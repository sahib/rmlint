Benchmarks
==========

This page contains the images that our benchmark suite renders for the current
release. Inside the benchmark suite, ``rmlint`` is *challenged* against other
popular and some less known duplicate finders. Apart from that a very dumb
duplicate finder called ``baseline.py`` is used to see how bad it could be.
We'll allow us a few remarks on 

.. image:: _static/benchmarks/timing.svg
   :width: 75%
   :align: center


.. image:: _static/benchmarks/cpu_usage.svg
   :width: 75%
   :align: center

.. image:: _static/benchmarks/memory.svg
   :width: 75%
   :align: center

.. image:: _static/benchmarks/found_items.svg
   :width: 75%
   :align: center

User benchmarks
---------------

If you like, you can add your own benchmarks below. 
Maybe include the following information:

- ``rmlint --version``
- ``uname -a`` or similar.
- Hardware setup, in particular the filesystem.
- The summary printed by ``rmlint`` in the end.
- Did it match your expectations?

If you have longer output you might want to use a pastebin like gist_.

.. _gist: https://gist.github.com/

.. raw:: html

   <div id="disqus_thread"></div>
   <script type="text/javascript">
       /* * * CONFIGURATION VARIABLES * * */
       var disqus_shortname = 'rmlint';
    
       /* * * DON'T EDIT BELOW THIS LINE * * */
       (function() {
           var dsq = document.createElement('script'); dsq.type = 'text/javascript'; dsq.async = true;
           dsq.src = '//' + disqus_shortname + '.disqus.com/embed.js';
           (document.getElementsByTagName('head')[0] || document.getElementsByTagName('body')[0]).appendChild(dsq);
       })();
   </script>
   <noscript>Please enable JavaScript to view the <a href="https://disqus.com/?ref_noscript" rel="nofollow">comments powered by Disqus.</a></noscript>

   <script type="text/javascript">
    /* * * CONFIGURATION VARIABLES * * */
    var disqus_shortname = 'rmlint';
    
    /* * * DON'T EDIT BELOW THIS LINE * * */
    (function () {
        var s = document.createElement('script'); s.async = true;
        s.type = 'text/javascript';
        s.src = '//' + disqus_shortname + '.disqus.com/count.js';
        (document.getElementsByTagName('HEAD')[0] || document.getElementsByTagName('BODY')[0]).appendChild(s);
    }());
    </script>
