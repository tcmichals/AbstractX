---
name: boundary-race-review
runs_on: host
verifies: "State shared across execution contexts (ISR, other core, DMA/worker, Linux peer, FPGA) has one named owner and a stated synchronisation, and the peer is treated as possibly dead or lying ([SPEC-AUDIT-15])."
description: "Check skill. Inventory of shared state (globals written in ISRs, volatile globals, handles posted to the dispatcher) with a required '// shared: <owner>; <sync>' note on each, plus a review checklist the agent answers per boundary. Run when changing ISRs, cross-core or cross-processor code, queues, the dispatcher, DMA completion paths or transports."
---

# Skill: boundary-race-review

**What it verifies**: races and lifetime bugs where two execution contexts meet. TSan only sees host
threads; it never sees an ISR, a second core, DMA, the Linux side of RPMsg or the FPGA. This skill is
the review for those boundaries.

## How to run

```bash
python3 tools/boundary_review.py --paths <changed dirs>   # inventory, with notes
python3 tools/boundary_review.py --check                  # gate: no shared variable without a note
```

Then answer the checklist below for every boundary the change touches. Write each answer as the
`// shared:` note on the declaration (owner and synchronisation) and summarise the rest in the commit
message.

## Checklist (answer per shared item)

1. **Owner.** Who writes it, who reads it? More than one writer needs a lock or an MPSC structure
   ([SPEC-TLP-03]).
2. **Synchronisation.** `std::atomic` with which order, interrupts masked, or single-core and
   word-sized? `volatile` alone is not synchronisation between cores.
3. **Check-then-act.** Is there a test followed by an action that the other side can invalidate in
   between (lost wakeup, WFI race, flag cleared by a second consumer)?
4. **Peer dead or lying.** If the other side stops, answers twice, or writes a bad index, length or
   address, does this side stay bounded and recover ([SPEC-E906-IPC-02])?
5. **Lifetime.** Does every pointer, span or handle that crosses the boundary point at storage that
   outlives the completion (AGENTS §10.3)? Can the coroutine be destroyed while the ISR holds its
   handle?
6. **Read once.** Is any field the other side can change read twice in one decision?
7. **Test.** Is there a test that drives this boundary the way production does, including the
   failure side (a fake that never recovers)?

## Pass criteria

`boundary_review.py --check` exits 0 for the touched paths, and the checklist is answered in the
commit message for each touched boundary.

## Evidence

The inventory output (`ok` lines with notes) and the checklist answers in the commit message.
