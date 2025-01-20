import argparse
import json
import pathlib

import pandas as pd
import scipy.stats


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog='compare_benchmarks',
        description='Compare compiler benchmark results',
    )

    parser.add_argument('--before', type=pathlib.Path, required=True, help='Old benchmark results (before PR applied)')
    parser.add_argument('--after', type=pathlib.Path, required=True, help='New benchmark results (after PR applied)')

    return parser.parse_args()


def split_benchmark_impl(row: pd.Series) -> tuple[str, str]:
    parts = row['name'].split('/')
    return (parts[0], parts[1])


def load_single_results(path: pathlib.Path, label: str) -> pd.DataFrame:
    with open(path, 'r') as f:
        json_data = json.load(f)

    df = pd.json_normalize(json_data['benchmarks'])
    df = df[df.name.str.endswith('_mean')]
    df.name = df.name.str.removesuffix('_mean')
    df = df.drop(
        [
            'family_index',
            'per_family_instance_index',
            'run_name',
            'run_type',
            'cpu_time',
            'repetitions',
            'threads',
        ],
        axis='columns',
    )

    df['version'] = label
    df[['name', 'implementation']] = df.apply(split_benchmark_impl, axis='columns', result_type='expand')
    df = df[df.implementation == 'compiled']
    return df


def load_results(before: pathlib.Path, after: pathlib.Path) -> pd.DataFrame:
    df = pd.concat([load_single_results(before, 'before'), load_single_results(after, 'after')]).reset_index(drop=True)
    assert len(set(df.time_unit.tolist())) == 1
    return df


def compute_ratio(df: pd.DataFrame) -> pd.DataFrame:
    df = df[['name', 'version', 'real_time', 'time_unit']].pivot(index='name', columns=['version'])
    df['change'] = df['real_time']['before'] / df['real_time']['after']
    return df


def relabel_for_printing(df: pd.DataFrame) -> pd.DataFrame:
    df['Before (ns)'] = df['real_time']['before']
    df['After (ns)'] = df['real_time']['after']
    df['Change (×)'] = df['change']

    cols = pd.Index(['Before (ns)', 'After (ns)', 'Change (×)'])
    df = df[cols]
    df.columns = cols
    df.index = pd.Index(map(lambda s: f'`{s}`', df.index.tolist()))
    df.index.names = ['Benchmark']

    return df


def to_markdown_with_stats(df: pd.DataFrame) -> str:
    main_table = df.to_markdown()

    geomean = scipy.stats.gmean(df['Change (×)'])
    geomean_row = f'| **Geometric Mean** | | | **{geomean:.6g}** |'
    return f'{main_table}\n{geomean_row}'


def main() -> None:
    args = parse_args()
    df = compute_ratio(load_results(args.before, args.after))
    df = relabel_for_printing(df)
    print(to_markdown_with_stats(df))


if __name__ == "__main__":
    main()
