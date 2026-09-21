Fixed cluster initialization completing before every host had been actively health
checked once. The remaining work was tracked as a count of health check completions,
so a host that completed several checks (a short interval, a retried failure, a
passive health check result) could account for hosts that had not been checked at
all. The cluster now waits for the first result of each host.
