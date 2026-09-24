"""Convert the existing report and appendices into one paste-ready LaTeX file."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent

def escape(s):
    replacements = {
        '\\': r'\textbackslash{}', '&': r'\&', '%': r'\%', '$': r'\$',
        '#': r'\#', '_': r'\_', '{': r'\{', '}': r'\}',
        '~': r'\textasciitilde{}', '^': r'\textasciicircum{}',
        '\u2014': ', ', '–': '--', 'µ': r'\ensuremath{\mu}',
        'Δ': r'\ensuremath{\Delta}', 'σ': r'\ensuremath{\sigma}',
        'π': r'\ensuremath{\pi}', '×': r'\ensuremath{\times}',
        '≈': r'\ensuremath{\approx}', '−': r'\ensuremath{-}',
        '°': r'\ensuremath{^{\circ}}', '²': r'\ensuremath{^2}',
        '‘': '`', '’': "'", '“': '``', '”': "''",
        '→': r'\ensuremath{\rightarrow}',
    }
    return ''.join(replacements.get(c, c) for c in s)

def inline(s):
    pattern = r'(`[^`]+`|\*\*[^*]+\*\*|\[[^\]]+\]\(https?://[^)]+\))'
    out = []
    for token in re.split(pattern, s):
        if token.startswith('`') and token.endswith('`'):
            out.append(r'\texttt{' + escape(token[1:-1]) + '}')
        elif token.startswith('**') and token.endswith('**'):
            out.append(r'\textbf{' + escape(token[2:-2]) + '}')
        elif re.fullmatch(r'\[[^\]]+\]\(https?://[^)]+\)', token):
            label, url = re.match(r'\[([^\]]+)\]\(([^)]+)\)', token).groups()
            out.append(r'\href{' + url.replace('%', r'\%') + '}{' + escape(label) + '}')
        elif re.fullmatch(r'[0-9a-f]{64}', token):
            out.append(r'\seqsplit{' + token + '}')
        else:
            out.append(escape(token))
    return ''.join(out)

def convert(md, main=False):
    lines = md.splitlines()
    output = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith('```'):
            lang = line[3:].strip()
            output.append(r'\begin{lstlisting}' + ('[language=C]' if lang == 'c' else ''))
            i += 1
            while i < len(lines) and not lines[i].startswith('```'):
                # Listings are plain source, not escaped LaTeX.
                output.append(lines[i])
                i += 1
            output.append(r'\end{lstlisting}')
        elif line.startswith('!['):
            caption, path = re.fullmatch(r'!\[([^\]]+)\]\(([^)]+)\)', line).groups()
            orientation = 'angle=-90,' if path.endswith('keypad_potentiometer_setup.jpg') else ''
            output += [r'\begin{figure}[H]', r'\centering',
                       r'\includegraphics[' + orientation + r'width=\linewidth,height=0.65\textheight,keepaspectratio]{' + path + '}',
                       r'\caption{' + inline(caption) + '}', r'\end{figure}']
        elif line.startswith('|'):
            rows = []
            while i < len(lines) and lines[i].startswith('|'):
                row = [c.strip() for c in lines[i].strip().strip('|').split('|')]
                if not all(re.fullmatch('[-: ]+', c) for c in row):
                    rows.append(row)
                i += 1
            n = len(rows[0])
            width = (0.98 - 0.032*n)/n
            spec = '|'.join(r'>{\raggedright\arraybackslash}p{' + f'{width:.3f}' + r'\linewidth}' for _ in range(n))
            output += [r'\begingroup\small', r'\setlength{\tabcolsep}{5pt}', r'\begin{longtable}{|' + spec + '|}', r'\hline']
            for index, row in enumerate(rows):
                cells = [inline(c) for c in row]
                if index == 0:
                    cells = [r'\textbf{' + c + '}' for c in cells]
                output += [' & '.join(cells) + r' \\ \hline']
                if index == 0:
                    output.append(r'\endhead')
            output += [r'\end{longtable}', r'\endgroup']
            continue
        elif line.startswith('#'):
            level = len(line) - len(line.lstrip('#'))
            title = line[level:].strip()
            if main and level == 1:
                i += 1
                continue
            if not main and level == 1:
                output.append(r'\clearpage')
            command = 'section' if level <= 2 else 'subsection'
            output.append('\\' + command + '*{' + inline(title) + '}')
        elif line.startswith('- '):
            output.append(r'\begin{itemize}')
            while i < len(lines) and lines[i].startswith('- '):
                output.append(r'\item ' + inline(lines[i][2:]))
                i += 1
            output.append(r'\end{itemize}')
            continue
        elif re.match(r'^\d+\. ', line):
            output.append(r'\begin{enumerate}')
            while i < len(lines) and re.match(r'^\d+\. ', lines[i]):
                output.append(r'\item ' + inline(re.sub(r'^\d+\. ', '', lines[i])))
                i += 1
            output.append(r'\end{enumerate}')
            continue
        else:
            output.append(inline(line.rstrip()) + (r'\\' if line.endswith('  ') else ''))
        i += 1
    return '\n'.join(output)

preamble = r'''\documentclass[11pt,letterpaper]{article}
\usepackage[margin=1in]{geometry}
\usepackage[T1]{fontenc}
\usepackage[utf8]{inputenc}
\usepackage{lmodern}
\usepackage{amsmath,amssymb}
\usepackage{array,longtable}
\usepackage{xcolor}
\usepackage{listings}
\usepackage{seqsplit}
\usepackage{graphicx}
\usepackage{float}
\usepackage{microtype}
\usepackage[hidelinks]{hyperref}
\setlength{\parindent}{0pt}
\setlength{\parskip}{6pt}
\setlength{\emergencystretch}{3em}
\lstset{basicstyle=\ttfamily\scriptsize,breaklines=true,
  breakatwhitespace=false,columns=fullflexible,keepspaces=true,
  showstringspaces=false,frame=single,rulecolor=\color{gray!40},
  commentstyle=\color{black!60},tabsize=4}
\title{Programmable Birdsong Synthesizer\\\large ECE 4760: Laboratory 1}
\author{[GROUP MEMBERS AND NETIDS]}
\date{[SUBMISSION DATE]}
\begin{document}
\maketitle
% Upload this file and the figures folder into Overleaf.
% Scope captures and spectrograms remain placeholders, not fabricated results.
'''
parts = [preamble,
         convert((ROOT/'Lab_1_Report.md').read_text(encoding='utf-8'),main=True),
         convert((ROOT/'Appendix_A_Code.md').read_text(encoding='utf-8')),
         convert((ROOT/'Appendix_B_AI_Use_Log.md').read_text(encoding='utf-8')),
         r'\end{document}']
result = '\n\n'.join(parts)
(ROOT/'Lab_1_Report.tex').write_text(result,encoding='utf-8')
# Check structural completeness independently of a LaTeX installation.
for env in ['document','lstlisting','longtable','itemize','enumerate']:
    assert result.count('\\begin{'+env+'}') == result.count('\\end{'+env+'}'), env
assert r'\input{' not in result
print('Created standalone Lab_1_Report.tex; environment counts balanced.')
