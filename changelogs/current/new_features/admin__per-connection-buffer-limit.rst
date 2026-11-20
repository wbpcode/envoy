Added :ref:`per_connection_buffer_limit_bytes
<envoy_v3_api_field_config.bootstrap.v3.Admin.per_connection_buffer_limit_bytes>` to the admin
interface. When set, streamed admin responses such as ``/stats`` and ``/config_dump`` are flow
controlled: the next chunk of a response is produced only once the client has drained the
response buffered so far, so a slow client no longer makes Envoy buffer the whole response. If not
set, there is no limit, as before.
