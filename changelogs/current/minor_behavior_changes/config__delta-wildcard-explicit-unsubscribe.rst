The delta xDS client now represents a wildcard subscription explicitly (as the ``*`` resource
name) internally, rather than relying on an implicit "empty request means wildcard" state machine.
Steady-state requests are unchanged: a subscription whose only interest is the wildcard is still
sent as an empty resource list on the wire. The one observable change is on teardown -- when the
last watcher of the wildcard is removed while the stream is still established (for example, when a
route configuration using VHDS is removed), the client now sends an explicit
``resource_names_unsubscribe: ["*"]`` to tell the management server to stop the wildcard, whereas
previously it sent nothing and left the wildcard subscription active on the server.
