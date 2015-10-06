/*
 *  This file is part of rmlint.
 *
 *  rmlint is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  rmlint is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with rmlint.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *
 *  - Christopher <sahib> Pahl 2010-2015 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2015 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include "md-scheduler.h"

/* handy for comparing 64-bit integers and returning int */
#define SIGN_DIFF(X, Y) (((X) > (Y)) - ((X) < (Y)))

/* How many milliseconds to sleep if we encounter an empty file queue.
 * This prevents a "starving" RmShredDevice from hogging cpu and cluttering up
 * debug messages by continually recycling back to the joiner.
 */
#if _RM_MDS_DEBUG
#define MDS_EMPTYQUEUE_SLEEP_US (60 * 1000 * 1000) /* 60 seconds */
#else
#define MDS_EMPTYQUEUE_SLEEP_US (50 * 1000) /* 0.05 second */
#endif

///////////////////////////////////////
//            Structures             //
///////////////////////////////////////

struct _RmMDS {
    /* Structure for RmMDS object/session */

    /* The function called for each task */
    RmMDSFunc func;

    /* Threadpool for device workers */
    GThreadPool *pool;

    /* Sorting function for device task queues */
    RmMDSSortFunc prioritiser;

    /* Mounts table for grouping dev's by physical devices
     * and identifying rotationality */
    RmMountTable *mount_table;

    /* If true then don't use mount table; interpret user-supplied dev as disk id */
    bool fake_disk;

    /* Table of physical disk/devices */
    GHashTable *disks;

    /* Lock for access to:
     *  self->disks
     */
    GMutex lock;
    GCond cond;

    /* flag for whether threadpool is running */
    gboolean running;

    /* quota to limit number of tasks per pass of each device */
    gint pass_quota;

    /* maximum number of threads and threads per disk */
    gint max_threads;
    gint threads_per_disk;

    /* pointer to user data to be passed to func */
    gpointer user_data;

    /* number of pending tasks */
    guint pending_tasks;

    volatile int aborted;
};

typedef struct _RmMDSDevice {
    /* Structure containing data associated with one Device worker thread */

    /* The RmMDS session parent */
    RmMDS *mds;

    /* Device's physical disk ID (only used for debug info) */
    dev_t disk;

    /* Sorted list of tasks queued for execution */
    GSList *sorted_tasks;

    /* Stack for tasks that will be sorted and carried out next pass */
    GSList *unsorted_tasks;

    /* Lock for access to:
     *  self->sorted_tasks
     *  self->unsorted_tasks
     *  self->ref_count
     */
    GMutex lock;
    GCond cond;

    /* Reference count for self */
    gint ref_count;

    /* Number of running threads for self */
    gint threads;

    /* is disk rotational? */
    gboolean is_rotational;

} RmMDSDevice;

//////////////////////////////////////////////
//  Internal Structure Init's & Destroyers  //
//////////////////////////////////////////////

/* RmMDSTask */
static RmMDSTask *rm_mds_task_new(const dev_t dev, const guint64 offset,
                                  const gpointer task_data) {
    RmMDSTask *self = g_slice_new0(RmMDSTask);
    self->dev = dev;
    self->offset = offset;
    self->task_data = task_data;
    return self;
}

static void rm_mds_task_free(RmMDSTask *task) {
    g_slice_free(RmMDSTask, task);
}

/* RmMDSDevice */
static RmMDSDevice *rm_mds_device_new(RmMDS *mds, const dev_t disk) {
    RmMDSDevice *self = g_slice_new0(RmMDSDevice);

    g_mutex_init(&self->lock);
    g_cond_init(&self->cond);

    self->mds = mds;
    self->ref_count = 0;
    self->threads = 0;
    self->disk = disk;

    if (mds->fake_disk) {
        self->is_rotational = (disk % 2 == 0);
    } else {
        self->is_rotational = !rm_mounts_is_nonrotational(mds->mount_table, disk);
    }

    rm_log_debug_line("Created new RmMDSDevice for %srotational disk #%lu",
            self->is_rotational ? "" : "non-", (long unsigned)disk);
    return self;
}

/** @brief  Free mem allocated to a RmMDSDevice
 **/
static void rm_mds_device_free(RmMDSDevice *self) {
    g_mutex_clear(&self->lock);
    g_cond_clear(&self->cond);
    g_slice_free(RmMDSDevice, self);
}


///////////////////////////////////////
//    RmMDSDevice Implementation   //
///////////////////////////////////////


/** @brief Mutex-protected task pusher
 **/

static void rm_mds_push_task_impl(RmMDSDevice *device, RmMDSTask *task) {
    g_mutex_lock(&device->lock);
    {
        device->unsorted_tasks =
                g_slist_prepend(device->unsorted_tasks, task);
        g_cond_signal(&device->cond);
    }
    g_mutex_unlock(&device->lock);
}

/** @brief Mutex-protected task popper
 **/

static RmMDSTask *rm_mds_pop_task(RmMDSDevice *device) {
    RmMDSTask *task = NULL;
    g_mutex_lock(&device->lock);
    {
        if(device->sorted_tasks) {
            task = device->sorted_tasks->data;
            device->sorted_tasks = g_slist_delete_link(device->sorted_tasks, device->sorted_tasks);
        }
    }
    g_mutex_unlock(&device->lock);
    return task;
}



/** @brief GCompareDataFunc wrapper for mds->prioritiser
 **/
static gint rm_mds_compare(const RmMDSTask *a, const RmMDSTask *b,
                           RmMDSSortFunc prioritiser) {
    gint result = prioritiser(a, b);
    return result;
}


/** @brief RmMDSDevice worker thread
 **/
static void rm_mds_factory(RmMDSDevice *device, RmMDS *mds) {
    /* rm_mds_factory processes tasks from device->task_list.
     * After completing one pass of the device, returns self to the
     * mds->pool threadpool. */
    gint processed = 0;
    g_mutex_lock(&device->lock);
    {
        /* check for empty queues - if so then wait a little while before giving up */
        if(!device->sorted_tasks && !device->unsorted_tasks && device->ref_count > 0) {
            /* timed wait for signal from rm_mds_push_task_impl() */
            gint64 end_time = g_get_monotonic_time() + MDS_EMPTYQUEUE_SLEEP_US;
            g_cond_wait_until(&device->cond, &device->lock, end_time);
        }

        /* sort and merge task lists */
        if(device->unsorted_tasks) {
            device->sorted_tasks = g_slist_concat(
                    g_slist_sort_with_data(
                        device->unsorted_tasks,
                        (GCompareDataFunc)rm_mds_compare,
                        (RmMDSSortFunc)mds->prioritiser),
                    device->sorted_tasks);

                device->unsorted_tasks = NULL;
        }
    }
    g_mutex_unlock(&device->lock);

    /* process tasks from device->sorted_tasks */
    RmMDSTask *task = NULL;
    while(processed < mds->pass_quota && (task = rm_mds_pop_task(device)) ) {
        if ( mds->func(task->task_data, mds->user_data) ) {
            /* task succeeded */
            rm_mds_task_free(task);
            /* decrement counters */
            ++processed;
            g_atomic_int_dec_and_test(&device->mds->pending_tasks);
        } else {
            /* task failed; push it back to device->unsorted_tasks */
            rm_mds_push_task_impl(device, task);
        }
    }

    if(rm_mds_device_ref(device, 0) > 0) {
        /* return self to pool for further processing */
        if (processed == 0) {
            /* stalled queue; chill for a bit */
            g_usleep(10000);
        }
        rm_util_thread_pool_push(mds->pool, device);
    } else {
        /* free self and signal to rm_mds_free() */
        if (g_atomic_int_dec_and_test(&device->threads)) {
            rm_mds_device_free(device);
            g_cond_signal(&mds->cond);
        }
    }
}

/** @brief Push a RmMDSDevice to the threadpool
 **/
void rm_mds_device_start(_U  gpointer disk, RmMDSDevice *device, RmMDS *mds) {
    g_assert(device->threads == 0);

    device->threads = mds->threads_per_disk;
    for (int i=0; i < mds->threads_per_disk; ++i) {
        rm_util_thread_pool_push(mds->pool, device);
    }
}

void rm_mds_start(RmMDS *mds) {
    guint disks = g_hash_table_size(mds->disks);
    guint threads = CLAMP(mds->threads_per_disk * disks, 1, (guint)mds->max_threads);

    mds->pool = rm_util_thread_pool_new((GFunc)rm_mds_factory, mds, threads);
    mds->running = TRUE;

    g_hash_table_foreach(mds->disks, (GHFunc)rm_mds_device_start, mds);
}

static RmMDSDevice *rm_mds_device_get_by_disk(RmMDS *mds, const dev_t disk) {
    RmMDSDevice *result = NULL;
    g_mutex_lock(&mds->lock);
    {
        g_assert(mds->disks);

        result = g_hash_table_lookup(mds->disks, GINT_TO_POINTER(disk));
        if(!result) {
            result = rm_mds_device_new(mds, disk);
            g_hash_table_insert(mds->disks, GINT_TO_POINTER(disk), result);
            if(g_atomic_int_get(&mds->running) == TRUE) {
                rm_mds_device_start(NULL, result, mds);
            }
        }
    }
    g_mutex_unlock(&mds->lock);
    return result;
}

//////////////////////////
//  API Implementation  //
//////////////////////////

RmMDS *rm_mds_new(const gint max_threads, RmMountTable *mount_table, bool fake_disk) {
    RmMDS *self = g_slice_new0(RmMDS);

    g_mutex_init(&self->lock);
    g_cond_init(&self->cond);

    self->max_threads = max_threads;

    if (!mount_table && !fake_disk) {
        self->mount_table = rm_mounts_table_new(FALSE);
    } else {
        self->mount_table = mount_table;
    }

    self->fake_disk = fake_disk;
    self->disks = g_hash_table_new(g_direct_hash, g_direct_equal);
    self->running = FALSE;

    return self;
}

void rm_mds_abort(RmMDS *mds) {
    g_mutex_lock(&mds->lock); {
        g_atomic_int_set(&mds->aborted, 1);

        /* Make sure rm_mds_finish() wakes up */
        g_cond_signal(&mds->cond);
    }
    g_mutex_unlock(&mds->lock);
}

void rm_mds_configure(RmMDS *self,
                      const RmMDSFunc func,
                      const gpointer user_data,
                      const gint pass_quota,
                      const gint threads_per_disk,
                      RmMDSSortFunc prioritiser) {
    g_assert(self->running == FALSE);
    self->func = func;
    self->user_data = user_data;
    self->threads_per_disk = threads_per_disk;
    self->pass_quota = (pass_quota>0) ? pass_quota : G_MAXINT;
    self->prioritiser = prioritiser;
}

void rm_mds_finish(RmMDS *mds) {
    /* wait for any pending threads to finish */
    while(g_atomic_int_get(&mds->pending_tasks) > 0 && g_atomic_int_get(&mds->aborted) == 0) {
        /* wait for a device to finish */
        g_mutex_lock(&mds->lock); {
            g_cond_wait(&mds->cond, &mds->lock);
        }
        g_mutex_unlock(&mds->lock);
    }
    mds->running = FALSE;
}

void rm_mds_free(RmMDS *mds, gboolean free_mount_table) {
    rm_mds_finish(mds);

    g_thread_pool_free(mds->pool, false, true);
    g_hash_table_destroy(mds->disks);

    if(free_mount_table && mds->mount_table) {
        rm_mounts_table_destroy(mds->mount_table);
    }
    g_mutex_clear(&mds->lock);
    g_cond_clear(&mds->cond);
    g_slice_free(RmMDS, mds);
}

gint rm_mds_device_ref(RmMDSDevice *device, const gint ref_count) {
    gint result = 0;
    g_mutex_lock(&device->lock);
    {
        device->ref_count += ref_count;
        result = device->ref_count;
    }
    g_mutex_unlock(&device->lock);
    return result;
}

RmMDSDevice *rm_mds_device_get(RmMDS *mds, const char *path, dev_t dev){
    dev_t disk = 0;
    if (dev == 0) {
        dev = rm_mounts_get_disk_id_by_path(mds->mount_table, path);
    }
    if(mds->fake_disk) {
        disk = dev;
    } else {
        disk = rm_mounts_get_disk_id(mds->mount_table, dev, path);
    }
    return rm_mds_device_get_by_disk(mds, disk);
}

gboolean rm_mds_device_is_rotational(RmMDSDevice *device) {
    return device->is_rotational;
}


void rm_mds_push_task(RmMDSDevice *device, dev_t dev, gint64 offset, const char *path, const gpointer task_data) {
    g_atomic_int_inc(&device->mds->pending_tasks);
    if(device->is_rotational && offset < 0) {
        offset = rm_offset_get_from_path(path, 0, NULL);
    }

    RmMDSTask *task = rm_mds_task_new(dev, offset, task_data);
    rm_mds_push_task_impl(device, task);
}

/**
 * @brief prioritiser function for basic elevator algorithm
 **/
gint rm_mds_elevator_cmp(const RmMDSTask *task_a, const RmMDSTask *task_b) {
    return (2 * SIGN_DIFF(task_a->dev, task_b->dev) +
            1 * SIGN_DIFF(task_a->offset, task_b->offset));
}
