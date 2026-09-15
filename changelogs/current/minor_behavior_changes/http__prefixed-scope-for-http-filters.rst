The HTTP filters of the connection manager are now handed the ``http.<stat_prefix>.`` scope of the
connection manager as the prefixed scope of their factory context, and the stats prefix that the
filter factories read through ``ExtraFactoryContext::statsPrefixOr()`` is empty instead of that same
``http.<stat_prefix>.`` string. Stat names, tag extracted names and tags are unchanged: the prefix
only moves from the name that each filter builds to the name of the scope the filter creates its
stats in. The filters whose stats would not have survived that move unchanged keep creating them in
the unprefixed scope with the full stats prefix.
This change can be reverted by setting the runtime guard
``envoy.reloadable_features.use_prefixed_scope_for_http_filter`` to false.
