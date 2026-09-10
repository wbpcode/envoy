The HTTP filters of the connection manager are now created with that ``http.<stat_prefix>.`` scope
as the scope of their factory context, and the stats prefix that is propagated to the filter
factories is empty instead of that same ``http.<stat_prefix>.`` string.
This part of the change can be reverted by setting the runtime guard
``envoy.reloadable_features.use_prefixed_scope_for_http_filter`` to false.
