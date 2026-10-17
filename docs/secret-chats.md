# Secret-chat state and verification

PFS rotates transport keys without changing the visual authentication hash.
The hash retains the original key's SHA-1 prefix and, from layer 46, the
transition key's SHA-256 suffix, following the
[Telegram specification](https://core.telegram.org/api/end-to-end/pfs#key-visualization)
and the Nagram, Nagram XF and Extraless implementations.

The sealed chat record appends the visual hash and pending exchange state after
existing fields. Both exchange roles can resume; orphan inbound reservations are
cleared. Only an unanswered initiating exchange expires. DH failures and stale
callbacks may release only their own reservation. Rekeying requires peer layer
46+, including at asynchronous completion. A valid new-key packet also
completes the exchange when CommitKey is lost; the previous key is kept
for gaps.

The previous transport key is retained across sequence gaps and discarded only
when a valid current-key packet reaches ordered processing. Deferred packets
retain their fingerprint so filling a gap applies the same rule.

Legacy records cannot recover an original hash lost by an earlier rotation.
Migration preserves the value shown before upgrade and initializes absent usage
accounting to the current time and zero messages.

Incoming requests are declined only by the explicit Decline button. Closing the
dialog preserves the request; presentation waits for navigation to finish.

## Verification

`Secret chat checks` compiles the production PFS decision and wire-format tests
directly. They cover thresholds, simultaneous exchanges, DH-failure release, layer gates,
service constructors and truncated packets; they do not exercise an authenticated client session.

Official-client acceptance still requires both rotation directions, a restart
during exchange, bidirectional delivery and comparison of visual hashes. Passive
dialog dismissal/reopen and explicit Decline also require live verification.
Private profiles, logs and screenshots must never be committed.
