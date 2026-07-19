Removed the runtime guard ``envoy.reloadable_features.use_migration_in_quiche`` and the legacy code
path it guarded. QUICHE now always handles QUIC connection migration and server preferred address
migration for client connections.
