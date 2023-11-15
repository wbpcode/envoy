FROM jaegertracing/all-in-one@sha256:55f8edb2798935a3f5cddf498d7a763d3091bdec4e65abba5d94fc6a7c6140a8
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
