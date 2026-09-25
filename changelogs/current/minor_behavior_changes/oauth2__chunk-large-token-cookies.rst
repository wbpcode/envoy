OAuth2 token cookies larger than 4096 bytes are now split across multiple cookies and rejoined when
the request comes back. Browsers reject a cookie that large, so an access, ID or refresh token above
that size was silently dropped by the client and the filter redirected the user to the authorization
server on every request. A token that still fits in one cookie is written exactly as before.

A token is split into at most 5 cookies, as ``<name>_0`` .. ``<name>_N-1`` alongside a
``<name>_chunks`` cookie holding the count. A token too large even for that refuses the session
rather than writing cookies the client cannot store. A session that splits all three tokens may need
``max_request_headers_kb`` raised. Because a large ID token now survives where it was previously
dropped, a configured ``end_session_endpoint`` receives a correspondingly large ``id_token_hint``,
which some providers may reject.

New counters ``oauth_token_cookie_chunked``, ``oauth_token_cookie_reassembled``,
``oauth_token_cookie_malformed_chunks`` and ``oauth_token_cookie_oversized`` report this activity.

This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.oauth2_chunk_large_token_cookies`` to ``false``. That is worth doing
during a rolling upgrade if a client may reach an instance that predates this change, since such an
instance cannot rejoin the cookies and would restart the authorization flow.
