"""Plot accepted Pico Sports trials and export per-trial/category feature tables."""
import argparse
import csv
from pathlib import Path
import statistics
import math


def load_trials(path):
    trials = {}
    with Path(path).open(newline='') as f:
        for row in csv.DictReader(f):
            trials.setdefault(row['trial_id'], []).append(row)
    for rows in trials.values():
        rows.sort(key=lambda r: int(r['elapsed_us']))
    return trials


def norm(row, columns):
    return math.sqrt(sum(float(row[c])**2 for c in columns))


def features(rows, corrected=True, accel_threshold=2.0, gyro_threshold=0.5):
    prefix = 'corrected_' if corrected else ''
    ac = [prefix+c for c in ('ax_mps2', 'ay_mps2', 'az_mps2')]
    gc = [prefix+c for c in ('gx_rads', 'gy_rads', 'gz_rads')]
    motion = [r for r in rows if r['phase'] == 'motion']
    if not motion:
        raise ValueError('Trial has no motion-phase samples')
    a = [norm(r, ac) for r in motion]
    g = [norm(r, gc) for r in motion]
    active = [int(r['elapsed_us']) for r, av, gv in zip(motion, a, g)
              if abs(av-9.80665) > accel_threshold or gv > gyro_threshold]
    span = (int(rows[-1]['pico_us'])-int(rows[0]['pico_us']))/1e6
    result = dict(trial_id=rows[0]['trial_id'], throw_category=rows[0]['throw_category'],
                  repetition=int(rows[0]['repetition']), sample_count=len(rows),
                  measured_hz=(len(rows)-1)/span if span else 0,
                  peak_accel_mps2=max(a), rms_accel_mps2=math.sqrt(statistics.mean(v*v for v in a)),
                  peak_gyro_rads=max(g),
                  motion_duration_s=(active[-1]-active[0])/1e6 if len(active)>1 else 0,
                  near_rail_samples=sum(int(r['near_rail']) for r in rows))
    return result


def category_stats(results):
    output = []
    for category in sorted({r['throw_category'] for r in results}):
        group = [r for r in results if r['throw_category'] == category]
        item = dict(throw_category=category, trials=len(group))
        for key in ('peak_accel_mps2', 'rms_accel_mps2', 'peak_gyro_rads', 'motion_duration_s'):
            values = [r[key] for r in group]
            item[key+'_mean'] = statistics.mean(values)
            item[key+'_std'] = statistics.stdev(values) if len(values)>1 else ''
        output.append(item)
    return output


def write_table(path, rows):
    with path.open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def make_plots(trials, selected, category, directory, corrected):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    prefix = 'corrected_' if corrected else ''
    ac = [prefix+c for c in ('ax_mps2', 'ay_mps2', 'az_mps2')]
    gc = [prefix+c for c in ('gx_rads', 'gy_rads', 'gz_rads')]
    rows = trials[selected]
    times = [int(r['elapsed_us'])/1e6 for r in rows]
    fig, axes = plt.subplots(4, 1, figsize=(10, 9), sharex=True, constrained_layout=True)
    for axis, columns, ylabel in ((axes[0], ac, 'Acceleration (m/s²)'), (axes[1], gc, 'Angular velocity (rad/s)')):
        for c, label in zip(columns, 'XYZ'):
            axis.plot(times, [float(r[c]) for r in rows], label=label)
        axis.set_ylabel(ylabel)
        axis.legend(loc='upper right', ncol=3)
    axes[2].plot(times, [norm(r, ac) for r in rows], color='darkorange')
    axes[2].set_ylabel('|a| (m/s²)')
    axes[3].plot(times, [norm(r, gc) for r in rows], color='teal')
    axes[3].set_ylabel('|ω| (rad/s)')
    for ax in axes:
        for phase, color in (('pre', 'lightgray'), ('motion', 'lightblue'), ('post', 'lightgray')):
            ts = [t for t, r in zip(times, rows) if r['phase']==phase]
            if ts:
                ax.axvspan(ts[0], ts[-1], color=color, alpha=0.2)
        ax.grid(alpha=0.25)
    axes[3].set_xlabel('Time since recording start (s)')
    fig.suptitle(f'{rows[0]["throw_category"]} — trial {rows[0]["trial_number"]}\n'
                 f'{"Bias-corrected" if corrected else "Uncorrected"}; gravity retained; shaded motion phase is a cue')
    fig.savefig(directory/'selected_trial.png', dpi=150)
    plt.close(fig)
    fig, axes = plt.subplots(2, 1, figsize=(10, 7), sharex=True, constrained_layout=True)
    for tid, group in trials.items():
        if group[0]['throw_category'] != category:
            continue
        motion = [r for r in group if r['phase']=='motion']
        if not motion:
            continue
        # Align by first sampled motion-phase cue, not inferred physical release.
        origin = int(motion[0]['elapsed_us'])
        ts = [(int(r['elapsed_us'])-origin)/1e6 for r in group]
        for ax, cols in zip(axes, (ac, gc)):
            ax.plot(ts, [norm(r, cols) for r in group], alpha=0.7,
                    label=f'Rep {group[0]["repetition"]}')
    axes[0].set_ylabel('|a| (m/s²)'); axes[1].set_ylabel('|ω| (rad/s)')
    axes[1].set_xlabel('Seconds relative to first motion-phase sample')
    for ax in axes:
        ax.grid(alpha=0.25)
    axes[0].legend(ncol=5, fontsize=8)
    fig.suptitle(f'{category}: repeated throws')
    fig.savefig(directory/'category_comparison.png', dpi=150)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--trial', help='Trial ID or trial_number (1..50); defaults to first accepted')
    parser.add_argument('--category')
    parser.add_argument('--raw', action='store_true', help='Use uncorrected SI values')
    parser.add_argument('--accel-threshold', type=float, default=2)
    parser.add_argument('--gyro-threshold', type=float, default=0.5)
    parser.add_argument('--out', type=Path, default=Path('analysis'))
    args = parser.parse_args()
    if not all(math.isfinite(v) and v>=0 for v in (args.accel_threshold,args.gyro_threshold)):
        parser.error('Thresholds must be finite and nonnegative')
    trials = load_trials(args.csv)
    if not trials:
        parser.error('No accepted throws in CSV')
    selected = next(iter(trials))
    if args.trial:
        matches = [tid for tid, rows in trials.items() if tid==args.trial or rows[0]['trial_number']==args.trial]
        if len(matches)!=1:
            parser.error('Trial not found or ambiguous')
        selected = matches[0]
    category = args.category or trials[selected][0]['throw_category']
    if not any(r[0]['throw_category']==category for r in trials.values()):
        parser.error('Category has no accepted throws')
    # Unique output directory prevents overwriting previous analysis artifacts.
    from datetime import datetime
    directory = args.out / datetime.now().strftime('%Y%m%d_%H%M%S_%f')
    directory.mkdir(parents=True, exist_ok=False)
    results = [features(rows, not args.raw, args.accel_threshold, args.gyro_threshold) for rows in trials.values()]
    write_table(directory/'trial_features.csv', results)
    write_table(directory/'category_stats.csv', category_stats(results))
    make_plots(trials, selected, category, directory, not args.raw)
    (directory/'notes.txt').write_text(
        f'Input: {args.csv.resolve()}\nBias corrected: {not args.raw}\n'
        f'Activity thresholds: abs(|a|-g)>{args.accel_threshold} m/s^2 OR |gyro|>{args.gyro_threshold} rad/s\n'
        'Features use the prompted motion phase. Acceleration magnitude and RMS retain gravity.\n'
        'Duration spans first to last above-threshold sample, not exact release/motion ground truth.\n'
        'Category standard deviations use n-1; empty for a single trial.\n')
    print(f'Analyzed {len(trials)} trials. Results: {directory.resolve()}')


if __name__ == '__main__':
    main()
