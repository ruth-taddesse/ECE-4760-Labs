"""Generate a source snapshot appendix and a printable HTML draft, without editing firmware."""
from pathlib import Path
import hashlib
import html
import re

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
notes = {
    'dactest.c': 'Week 1 baseline: a fixed 800 Hz phase increment feeds channel A. The timer ISR performs the table lookup and SPI write.',
    'dactest_other_channel.c': 'Week 1 channel experiment: the DAC control word selects B rather than A. The DDS frequency remains 800 Hz.',
    'ADC_with_DDS.c': 'Week 1 integration: the ADC thread converts a potentiometer code to Hz, then to a phase increment. The timer ISR independently emits audio samples.',
    'ADC_DDS_keypad_mute.c': 'Week 2 mute stage: keypad scanning and a four-state debounce machine set tone_enabled; the amplitude thread ramps output rather than rebuilding the sine table.',
    'ADC_DDS_keypad_record.c': 'Week 2 recording stage: nine buffers store frequencies in Hz. The recording and playback threads both request 10 ms intervals. Historical snapshot, not the current build target.',
    'ADC_DDS_keypad_speedup.c': 'Final target: frequency recording at 10 ms, playback at 1 ms, soundboard previews during compose entry, and indirect sequence playback. Read the known limitations in the main report.',
    'CMakeLists.txt': 'Build configuration selects only ADC_DDS_keypad_speedup.c as the application source and targets pico2.',
}
annotations = {
    '//muting variables': '// REPORT NOTE: amplitude scales the signed sample; midpoint is added afterward.\n// In the final file, startup sound is enabled and the ramp yield is 50 us.\n',
    '//record variables': '// REPORT NOTE: frequency storage is RAM-only. Key k uses row k-1.\n// Valid lengths bound replay; capacity is 1000 samples per sound.\n',
    '//compose variables': '// REPORT NOTE: the sequence stores key IDs, not waveform data or delays.\n// current_button indexes the sequence; its value selects a sound slot.\n',
    'static PT_THREAD (protothread_toggle25': '// REPORT NOTE: live ADC updates must not replace a playback frequency.\n// Diagnostic printing and cooperative scheduling affect update timing.\n',
    'static PT_THREAD (protothread_core_0': '// REPORT NOTE: scan and debounce once per 30 ms yield cycle.\n// This snapshot dispatches controls on confirmed PRESS, not release.\n',
    '          case MAYBE_PRESSED:': '// REPORT NOTE: only this transition dispatches new press actions.\n',
    '          case PRESSED:': '// REPORT NOTE: the held state starts recording in an armed mode.\n// It is revisited while held, so do not reset the write index here every scan.\n',
    '          case MAYBE_NOT_PRESSED:': '// REPORT NOTE: two observations of loss of the remembered key end a hold.\n',
    'static PT_THREAD (protothread_amplitude': '// REPORT NOTE: clamp before publishing amplitude so ISR samples remain in range.\n',
    'static PT_THREAD (protothread_playback': '// REPORT NOTE: a frequency entry changes DDS increment, not the DAC sample directly.\n// Final composition advances entries in a separate iteration, adding a short dwell.\n',
    'static PT_THREAD (protothread_recording': '// REPORT NOTE: store mapped Hz, not the much larger 32-bit DDS increment.\n// The idle branch resets the write index; the source does not clear a slot at start.\n',
    'static void alarm_irq': '// REPORT NOTE: GPIO2 high/low brackets the instrumented synthesis body.\n// Rearming relative to the current timer introduces latency into the actual period.\n',
    '    DAC_data =': '// REPORT NOTE: sine sample is centered around zero before the +2048 offset.\n// The existing 0xffff mask is retained; valid bounds keep data in 12 bits.\n',
    'int main()': '// REPORT NOTE: initialize peripherals and sine table before scheduling control work.\n',
}
out = ['# Appendix A: Annotated Source Snapshot\n',
       'Snapshot date: September 20, 2026. Original application files are unchanged. '
       'Comments marked REPORT NOTE were inserted only in the final-source listing below. '
       'Other source text is preserved. These notes explain behavior, not implemented fixes.\n',
       '| File | SHA-256 of original bytes |\n|---|---|']
for name in notes:
    out.append(f'| {name} | {hashlib.sha256((ROOT/name).read_bytes()).hexdigest()} |')
for name, note in notes.items():
    text = (ROOT/name).read_text(encoding='utf-8-sig')
    if name == 'ADC_DDS_keypad_speedup.c':
        lines = []
        for line in text.splitlines(keepends=True):
            for marker, annotation in annotations.items():
                if line.startswith(marker):
                    lines.append(annotation)
            lines.append(line)
        text = ''.join(lines)
    lang = 'cmake' if name.endswith('.txt') else 'c'
    out += [f'\n## {name}\n', note+'\n', f'```{lang}\n{text.rstrip()}\n```\n']
(HERE/'Appendix_A_Code.md').write_text('\n'.join(out), encoding='utf-8')

def inline(s):
    s = html.escape(s)
    s = re.sub(r'`([^`]+)`', r'<code>\1</code>', s)
    s = re.sub(r'\*\*([^*]+)\*\*', r'<strong>\1</strong>', s)
    s = re.sub(r'\[([^\]]+)\]\((https?://[^)]+)\)', r'<a href="\2">\1</a>', s)
    return s

def render(md):
    result, paragraph, code = [], [], None
    table = False
    def flush():
        if paragraph:
            result.append('<p>'+inline(' '.join(paragraph))+'</p>')
            paragraph.clear()
    for line in md.splitlines():
        if line.startswith('```'):
            flush()
            if code is None:
                code = []
            else:
                result.append('<pre><code>'+html.escape('\n'.join(code))+'</code></pre>')
                code = None
            continue
        if code is not None:
            code.append(line)
            continue
        if line.startswith('|'):
            flush()
            cells = [x.strip() for x in line.strip('|').split('|')]
            if all(re.fullmatch(r'[-: ]+', c) for c in cells):
                continue
            if not table:
                result.append('<table>')
                table = True
                tag = 'th'
            else:
                tag = 'td'
            result.append('<tr>'+''.join(f'<{tag}>{inline(c)}</{tag}>' for c in cells)+'</tr>')
            continue
        if table:
            result.append('</table>')
            table = False
        if not line.strip():
            flush()
        elif line.startswith('!['):
            flush()
            caption, path = re.fullmatch(r'!\[([^\]]+)\]\(([^)]+)\)', line).groups()
            result.append('<figure style="break-inside:avoid;margin:1em 0"><img style="width:100%;height:auto" src="'+html.escape(path)+'" alt="'+html.escape(caption)+'"><figcaption>'+inline(caption)+'</figcaption></figure>')
        elif line.startswith('#'):
            flush()
            level = len(line)-len(line.lstrip('#'))
            result.append(f'<h{level}>'+inline(line[level:].strip())+f'</h{level}>')
        elif line.startswith('- ') or re.match(r'^\d+\. ', line):
            flush()
            result.append('<p class="item">'+inline(line)+'</p>')
        else:
            paragraph.append(line)
    flush()
    if table:
        result.append('</table>')
    return '\n'.join(result)

style = '''body{max-width:1000px;margin:3em auto;padding:0 2em;color:#17212b;font:16px/1.55 Georgia,serif}
h1,h2,h3{font-family:Arial,sans-serif;color:#123c55;line-height:1.2}h1{margin-top:2em}h2{margin-top:1.7em}
table{width:100%;border-collapse:collapse;margin:1em 0;font:13px/1.4 Arial,sans-serif;overflow-wrap:anywhere}
th,td{border:1px solid #bcc7cf;padding:8px;text-align:left;vertical-align:top}th{background:#eaf0f4}
pre{padding:12px;background:#f3f5f7;white-space:pre-wrap;overflow-wrap:anywhere;font:11px/1.4 Consolas,monospace}
code{font-family:Consolas,monospace}a{color:#165d87}.item{margin-left:1em}
@media print{body{margin:0;max-width:none;font-size:11pt}h1{break-before:page}h1:first-of-type{break-before:auto}h2,h3{break-after:avoid}pre{font-size:8pt}a{color:inherit}table{font-size:9pt}}
'''
parts = [(HERE/name).read_text(encoding='utf-8') for name in
         ['Lab_1_Report.md','Appendix_B_AI_Use_Log.md','Appendix_A_Code.md']]
page = '<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Lab 1: Birdsong Synthesizer</title><style>'+style+'</style></head><body>'
page += '\n'.join(render(part) for part in parts)+'</body></html>'
(HERE/'Lab_1_Report.html').write_text(page,encoding='utf-8')
print('Generated Appendix_A_Code.md and Lab_1_Report.html')
print('Source files snapshotted:', len(notes))
