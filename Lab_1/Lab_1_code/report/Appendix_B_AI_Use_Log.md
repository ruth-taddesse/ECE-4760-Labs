# Appendix B: AI Use and Prompt Provenance

**Status: partial reconstruction, not a complete transcript.** This appendix is based on the available code-development conversation. It must accompany, not replace, an export of the complete user/assistant dialogue. Exact total words exchanged and total additions/deletions/modifications suggested and accepted cannot be established from source snapshots alone. Repeated advice, manual edits, and the difference between an accepted idea and accepted code lines prevent a defensible exact total.

## Scope and counting convention

The table below counts design-change categories, not code lines or individual suggestion events. It is a qualitative adoption audit. It is not presented as the numeric accounting required by the lab. The group should state whether tool activity is included when computing final transcript totals.

| Change category discussed with AI | Evidence in the final source |
|---|---|
| Add breaks and correct debounce state typo | Cases contain breaks and valid states |
| Dispatch actions once rather than repeatedly while held | Most actions occur on confirmed press |
| Move control actions to confirmed release | Not adopted in the reviewed final source |
| Scale signed sine samples before adding midpoint | Present in ISR |
| Shorten fade with bounded increments | Step 103, clamped endpoints; final yield 50 µs |
| Register control, amplitude, recording, playback threads | Five threads registered |
| Use fixed frequency buffers with valid lengths | Nine 1,000-entry uint16_t arrays |
| Store Hz rather than a truncated phase increment | ADC-to-Hz map stored in recording thread |
| Compress frequency update interval for speedup | Record 10 ms; playback 1 ms |
| Prevent ADC overwrites during all playback | Both playback flags guard live updates |
| Index composed sounds through recorded key sequence | compose_sequence[current_button] - 1 |
| Cancel competing playback and reset indices | Present on principal mode transitions |
| Return to live tone through key 0 | Mode cancellation and immediate phase update present |
| Reject empty individual playback | Length check present |
| Reject empty composition entries / avoid boundary delay | Not implemented in reviewed snapshot |

No numeric accepted-change total is inferred from this table. Similar code does not prove which exact suggested lines the students accepted. A visible earlier tool edit did make changes to an earlier `ADC_DDS_keypad.c`; that file is not among the current source snapshots. Its changes concerned fade speed, release action, thread registration, and an undefined LED reference.

## Selected exact user prompts and response summaries

The quotations below preserve selected user wording, including spelling, but omit IDE/environment boilerplate. Assistant responses are summaries, not verbatim transcripts. The complete export must preserve both sides' actual words to satisfy the prompt-log requirement.

| Selected user prompt | Assistant response summary |
|---|---|
| "what variable controls to amplitude of sine wave?" | Identified sine-table scale, phase increment, and DAC midpoint roles |
| "can we control the amplitude as we write to the dac instead of the sine table?" | Proposed scaling the signed sample before adding midpoint |
| "will the current code work?" | Reviewed thread registration, LED naming, startup state, and fade duration |
| "can you check this version to see if it meets the requiremts, if you would change anything let us know. do not make any changes yourself" | Reviewed recording reachability, sample representation, indexing, and build selection |
| "OKay check now. Does this fix this issues" | Identified missing semicolon, invalid recording keys, and idle muting |
| "okay check now" | Rechecked mode handling and playback reset behavior |
| "okay check now, offer soluiton suggetsed on ant problems, except number 3. dont change any files" | Supplied scoped playback and release-transition suggestions |
| "Say we were to play this sound 8 times faster, do we just multiply the phase_incr_main in phase_accum_main += phase_incr_main by 8?" | Distinguished faster trajectory traversal from pitch multiplication |
| "check now, find any more problems if any, also check if modes get properly reset. in compose mode we do want sounds to play whe  we hit button just like a soundboard" | Preserved compose previews and examined reset conflicts and sequence indexing |
| "check for any more logical errors" | Reviewed sequence interruption, empty recordings, and boundary timing |
| "Yes so we want key 0 to bring us back to playing tone from potentiometer, in tone generator mode:" | Proposed cancelling other modes and restoring the ADC-derived increment |

Additional visible exchanges covered the initial state-machine diagram, semaphore versus shared-flag design, recording arrays, and DAC/IRQ playback behavior. They are not reproduced as an invented complete transcript here.

## Required final accounting

| Required item | Current status |
|---|---|
| Complete prompts and assistant responses | Attach full conversation export |
| Number of words exchanged | Not available; compute from that export with a stated rule |
| Additions suggested / accepted | Not established; reconcile proposals and accepted diffs |
| Deletions suggested / accepted | Not established; reconcile proposals and accepted diffs |
| Modifications suggested / accepted | Not established; reconcile proposals and accepted diffs |
| AI-generated experimental measurements | None |

For reproducible counts, decide whether an addition/deletion means a source line or a logical change. Count replacements consistently, avoid counting the same repeated proposal as several accepted changes, and retain dated source diffs if available. This appendix deliberately does not substitute an unsupported estimate for the required counts.
