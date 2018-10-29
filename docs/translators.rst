Translating ``rmlint``
======================

Rudimentary support for internationalization is provided via ``gettext``. 
Also see this Issue_ for a list of translators, current translations and a
wish-list of new translations.

.. _Issue: https://github.com/sahib/rmlint/issues/146

Adding new languages
--------------------

.. code-block:: bash

   # Fork a new .po file from the po-template (here swedish):
   $ msginit -i po/rmlint.pot -o po/se.po --locale se --no-translator

   # Edit the po/se.po file, the format is self describing
   $ vim po/se.po

   # .po files need to be compiled, but that's handled by scons already.
   $ scons
   $ scons install

   # You should see your changes now:
   $ LANG=se ./rmlint


If you'd like to contribute your new translation you want to do a pull request 
(if you really dislike that, you may also send the translation to us via mail).
Here_ is a small introduction on Pull Requests.

.. _Here: http://rmlint.readthedocs.org/en/latest/developers.html

Updating existing languages
---------------------------

.. code-block:: bash

    # Edit the file to your needs:
    $ vim po/xy.po

    # Install:
    $ scons install

    # Done
    $ LANG=xy ./rmlint

Marking new strings for translations
------------------------------------

If you want to mark strings in the C-code to be translated, 
you gonna need to mark them so the ``xgettext`` can find it.
The latter tool goes through the source and creates a template file
with all translations left out. 

.. code-block:: c

   /* Mark the string with the _() macro */
   fprintf(out, _("Stuff is alright: %s\n"), (alright) ? "yes" : "no");


It gets a little harder when static strings need to be marked, since they cannot be 
translated during compile time. You have to mark them first and translate them at a later point:

.. code-block:: c

   static const char * stuff = _N("Hello World");

   void print_world(void) {
       printf("World is %s\n", _(stuff));
   }

After you're done with marking the new strings, you have to update the
gettext files:

.. code-block:: bash

   $ scons gettext

Then, proceed to work with the relevant `*.po` file as described above.
