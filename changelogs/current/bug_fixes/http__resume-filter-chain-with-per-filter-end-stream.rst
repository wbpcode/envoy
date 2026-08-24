Fixed a bug where resuming a stopped HTTP filter chain computed the ``end_stream`` value from the
stream level end-of-stream flag instead of the end of stream observed at the resuming filter's own
position in the chain. A filter that stopped iteration and then injected only a part of the body
with ``injectDecodedDataToFilterChain()`` or ``injectEncodedDataToFilterChain()`` (for example
``ext_proc``, ``bandwidth_limit``, ``fault`` or ``file_system_buffer``) could make a later
buffering filter flush its buffered data with ``end_stream`` set. Every filter now tracks whether
the end of stream has arrived at its own position in the chain, and injecting data no longer
overwrites the stream level flag, so a partial injection can no longer cause an internal redirect
to be refused. This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.resume_with_per_filter_end_stream`` to ``false``.
