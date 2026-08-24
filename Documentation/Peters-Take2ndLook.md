# Peter's Take-2nd-Look

Code-review findings that came up during the `Documentation/CodeReviewTracking.md` review
(or general review work) where I didn't have enough context/confidence in the affected
function's behavior to decide on a fix in the moment. Parked here so the finding isn't
lost and doesn't need to be rediscovered from scratch. Once resolved (fixed, or decided
as won't-fix), move the entry to the relevant GitHub Issue/commit and delete it from here.

## Open

### Issue #318 - `apps/system/hcom/misc/espcp_utils.c` - `espcp_queue_event_messages()` blocking send

**Score:** 50

**Finding:** `espcp_open_esp32_events_message_queue()` opens `/Esp32Events` (`mq_maxmsg`=10)
with no `O_NONBLOCK` (`espcp_utils.c:110`), and `espcp_queue_event_messages()`'s `mq_send()`
(`espcp_utils.c:176`) has no timeout variant (`mq_timedsend`) or non-blocking check.

**Risk if unfixed:** if the queue's consumer ever stalls or falls behind (e.g. during a burst
of cell/network events -- callers are in `hcom_pppd.c`'s connect/disconnect/retry/AT-cmd event
handlers), every subsequent event-raising call on the producer thread blocks indefinitely with
no way to detect or recover from it.

**Proposed fix (not yet applied):** switch `mq_send()` to `mq_timedsend()` with a bounded
absolute-deadline timeout (pattern already used in `hcom_esp32_proc_xmit.c:281-293` via
`clock_gettime()` + `mq_timedreceive()`), log + return failure on timeout instead of blocking.
`mq_timedsend()` respects the timeout regardless of `O_NONBLOCK`, so `mq_open()`'s flags don't
need to change.

**Why parked:** I don't have first-hand knowledge of `espcp_queue_event_messages()`'s runtime
behavior/call context (what happens upstream if it returns failure/times out, how time-sensitive
callers are, what a good timeout value is) to be confident picking a timeout value or confirming
the drop-on-timeout behavior is safe for every caller. Needs a look at `hcom_pppd.c`'s call
sites and how they handle a `-1` return before committing to a fix.
