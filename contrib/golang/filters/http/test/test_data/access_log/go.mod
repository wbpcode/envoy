module example.com/access_log

go 1.22

require github.com/envoyproxy/envoy v1.24.0

require google.golang.org/protobuf v1.34.2

replace github.com/envoyproxy/envoy => ../../../../../../../
