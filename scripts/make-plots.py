#!/bin/env python3

import pandas as pd
import numpy as np
import matplotlib as mpl
import matplotlib.pyplot as plt
import matplotlib.patches as patches

import argparse
import os.path
import itertools

def splitlist(x):
    return x.split(',')

p = argparse.ArgumentParser(usage='plot output from scripts/data.sh')
p.add_argument('--noshow', help='Don not show the charts in an interactive popup', action='store_true')
p.add_argument('--out', help='An output directory for plot images', type=str)
p.add_argument('--tout', help='An output directory for data tables', type=str)
p.add_argument('--in', dest='inf', help='Input directory for csv', type=str, default='./results')
p.add_argument('--uarch', help='focus on uarch', type=str)
p.add_argument('--only', help='process only the given figure', type=splitlist)
args = p.parse_args()

plt.rcParams['axes.labelsize'] = 'large'
plt.rcParams['axes.titlesize'] = 'large'
# fix various random seeds so we get reproducible plots
# fix the mpl seed used to generate SVG IDs
mpl.rcParams['svg.hashsalt'] = 'foobar'

# numpy random seeds (used by e.g., jitter function below)
np.random.seed(123)

class UarchDef:
    def __init__(self, name, code, desc, *, l1=32, l2, l3):
        self.name = name
        self.code = code
        self.desc = desc
        self.l1 = l1 * 1024
        self.l2 = l2 * 1024
        self.l3 = l3 * 1024

skl = UarchDef('Skylake-S', 'SKL', 'i7-6700HQ @ 2.6 GHz', l2=256, l3=6144)

def mark_caches(ax, uarch, tweak=None):
    cache_boundaries = [uarch.l1, uarch.l2, uarch.l3, ax.get_xlim()[1]]
    cache_names      = ['L1',  'L2',  'L3', 'RAM']
    left = ax.get_xlim()[0]
    bottom = ax.get_ylim()[0]
    last = left
    even = True
    for b,n in zip(cache_boundaries, cache_names):
        first = ax.get_lines()[0].get_data()[0][0]
        if b > first: # skip the cache if it's outside the plot limits
            print('drawing', n, 'at', last, b)
            if even:
                ax.add_patch(patches.Rectangle((last,0), b - last, ax.get_ylim()[1], color='whitesmoke'))
            # All this transformation stuff is to put the labels in the middle of the regions
            # despite the logarithmic x-axis. So we need to transform to display pixels to do our
            # placement, and then back again.
            t = ax.transData
            def xt(t, x):
                return t.transform((x,0))[0]
            lastd = xt(t, last)
            rlimd = xt(t, b)
            ax.text(xt(t.inverted(), lastd + (rlimd - lastd) / 2), bottom + 2.5 + (tweak or 0), n, fontsize=20, ha='center')
        even = not even
        last = b

def base_plot(df, arglist, uarch, do_caches=True, alpha=0.2, ylim=0, lloc=None, markers=None, **base):
    ax = None

    for col, extra in arglist:
        eargs = {'marker' : 'o'}
        eargs.update(base)
        eargs.update(extra)
        ax = df.plot(x='Size', y=col, figsize=(9,6), alpha=alpha,
                    logx=True, linestyle='none', ax=ax, **eargs)

    if ylim is not None:
        ax.set_ylim(ylim)

    largs = dict(loc='center right', bbox_to_anchor=(1, lloc)) if lloc else {}

    for lh in plt.legend(**largs).legendHandles:
        lh._legmarker.set_alpha(1)

    if do_caches and uarch:
        mark_caches(ax, uarch)

    ax.set_xlabel('Region Size (bytes)')
    ax.set_ylabel('Fill Speed (GB/s)')

    ax.get_figure().tight_layout()

    return ax

def inpath(name):
    return os.path.join(args.inf, name + '.csv')

def maybe_save(df, ax, name):
    if args.out:
        where = os.path.join(args.out, name + '.svg')
        ax.get_figure().savefig(where)
        print('Figure saved to ', where)

    if args.tout:
        # save table
        # this line moves the index name to be the first column name instead
        # subf = subf.rename_axis(index=None, columns=subf.index.name)
        header = "---\nlayout: default\n---\n\n"
        tpath = os.path.join(args.tout, name + '.html')
        with open(tpath, 'w') as f:
            f.write(header + df.to_html())
        print('saved html table to', tpath)

def base_from_file(name, uarch, figname, title=None, lloc=None):
    if args.only and not figname in args.only:
        print('skipping ', figname)
        return None

    df = pd.read_csv(inpath(name))
    df = df[['Size', 'Trial', 'Algo', 'GB/s']]
    df = df[df['Algo'].isin(['fill0', 'fill1'])]
    df = df.set_index(['Size', 'Trial', 'Algo']).unstack()
    dft = df.copy() # save a copy at this point for the table
    # print(dft.head())
    df = df.droplevel(0, axis=1).reset_index()
    title = title if title else uarch.name + ' Fill Performance (' + uarch.desc + ')'
    ax = base_plot(df, arglist=[('fill0', {}), ('fill1', {'fillstyle' : 'none'})], ylim=0, uarch=uarch,
            title=title, ms=10, lloc=lloc)
    maybe_save(dft, ax, figname)

base_from_file('overall', skl, 'fig1')
base_from_file('sawtooth', None, 'sawtooth', title='Sawtooth, Yo')

###### plot 2 ######


def read_reshape(outfile, file, title, colmap, algos=['fill0', 'fill1'], ylim=None, xlim=None,
        ylabel='Events / Cacheline', l2pos = 0.5, tweak=None, uarch=skl, eargs=[{}], ms=7, base={}):

    if args.only and not outfile in args.only:
        print('skipping ', outfile)
        return None

    df = pd.read_csv(inpath(file))
    df = df[df['Algo'].isin(algos)]
    df = df.set_index(['Size', 'Trial', 'Algo']).unstack().reset_index() # reshape
    df = df[['Size','GB/s'] + list(colmap.keys())] # extract relevant columns

    df = df.rename(columns=colmap)
    df.columns = [' : '.join(reversed(col)).lstrip(' : ') if col[0] != 'GB/s' else col[1] for col in df.columns.values]
    df = df.groupby(by=('Size')).median().reset_index()
    # print('2 --------------\n', df.head())

    arglist = []
    for algo, extra in zip(algos, itertools.cycle(eargs)):
        arglist.append((algo, extra))

    ax = base_plot(df, arglist=arglist, alpha=1, do_caches=False,
        title=title, uarch=uarch, ylim=ylim, ms=ms, **base)

    if xlim:
        ax.set_xlim(left=xlim)

    if colmap:
        cols = [a + ' : ' + e for a in algos for e in colmap.values()]
        ax2 = df.plot(x='Size', y=cols, logx=True, figure=None, fillstyle='none',
                ax=ax, secondary_y=True, ms=ms)
        ax2.set_ylabel(ylabel)
        ax2.legend(loc='center right', bbox_to_anchor=(1, l2pos))

    mark_caches(ax, uarch, tweak)

    ax.set_xlabel('Region Size (bytes)')
    ax.legend(loc='center left',   bbox_to_anchor=(0, 0.5))

    ax.get_figure().tight_layout()

    maybe_save(df, ax, outfile)

icl = UarchDef('Ice Lake'     , 'icl'     , 'i5-1035G4 @ 3.5 GHz'  ,  l2=512 , l3=6*1024, l1=48)

all_uarches = [
    UarchDef('Sandy Bridge' , 'SNB'     , 'E5-1620 @ 3.6 GHz'     , l2=256 , l3=10240) ,
    UarchDef('Haswell'      , 'HSW'     , 'i7-4770 @ 3.4 GHz'     , l2=256 , l3= 8192) ,
    UarchDef('Zen 2'        , 'Zen2'    , 'AMD EPYC 7262'         , l2=512 , l3=16384) ,
    UarchDef('Skylake-X'    , 'SKX'     , 'Xeon W-2104 @ 3.2 GHz' , l2=1024, l3= 8448) ,
    UarchDef('Skylake-S'    , 'SKL'     , 'i7-6700 @ 3.4 GHz'     , l2=256 , l3= 8192) ,
    UarchDef('Cannon Lake'  , 'CNL'     , 'i3-8121U @ 2.2 GHz'    , l2=256 , l3= 4096) ,
    UarchDef('POWER9'       , 'POWER9'  , 'POWER9 @ 3.8 GHz'      , l2=512 , l3=10240) ,
    UarchDef('Graviton 2'   , 'gra2'    , 'Graviton 2 @ 2.5 GHz'  , l2=1024 , l3=32*1024, l1=64),
    UarchDef('Graviton 2'   , 'gra2'    , 'Graviton 2 @ 2.5 GHz'  , l2=1024 , l3=32*1024, l1=64),
]

if args.uarch:
    uarches = [u for u in all_uarches if u.code == args.uarch]
else:
    uarches = all_uarches

for uarch in uarches:
    lloc = 0.85 if uarch.code == 'gra2' else None
    base_from_file(os.path.join(uarch.code.lower(), 'remote'), uarch, 'fig6-' + uarch.code.lower(), lloc=lloc)

l2rename = {'l2-out-silent' : 'L2 Silent', 'l2-out-non-silent' : 'L2 Non Silent'}

read_reshape('fig2', 'l2-focus', 'Fill Performance : L2 Lines Out', l2rename, ylim=10, tweak=7)
read_reshape('fig3', 'l3-focus', 'Fill Performance : Uncore Tracker Writes', {'uncW' : 'uncW'}, ylim=10, l2pos=0.6, tweak=-0.9)
read_reshape('fig4', 'l2-focus', 'Fill Performance : L2 Lines Out', {'l2-out-silent' : 'L2 Silent'},
        algos=['fill0', 'fill1', 'alt01'], ylim=None, tweak=8)
read_reshape('fig5', 'l3-focus', 'Fill Performance : Uncore Tracker Writes', {'uncW' : 'uncW'},
        algos=['fill0', 'fill1', 'alt01'], ylim=10, l2pos=0.6, tweak=-1)

### Ice Lake ###

base_from_file('icl512/overall-warm',  icl, 'fig7a')
base_from_file('icl/overall-warm',     icl, 'fig7b')

read_reshape('fig8', 'icl/l2-focus', 'Ice Lake Fill : L2 Lines Out', l2rename,
        xlim=60000, ylim=10, tweak=7, uarch=icl)

read_reshape('fig9', 'icl/l3-focus', 'Ice Lake Fill : Uncore Tracker Writes', {'uncW' : 'uncW'},
        algos=['fill0', 'fill1', 'alt01'], ylim=0, l2pos=0.6, tweak=-1, uarch=icl)

read_reshape('fig10', 'icl/256-512', 'Foo', {},
        algos=['fill256_0', 'fill256_1', 'fill512_0', 'fill512_1'],
        eargs=[{'fillstyle':'left'},{'fillstyle':'right'},{'fillstyle':'left'},{'fillstyle':'right'}],
        ylim=0, l2pos=0.6, tweak=-1, base={'mew' : 0}, ms=10, uarch=icl)

read_reshape('fig11', 'icl/256-512-l2-l3', 'Foo', l2rename,
        algos=['fill256_0', 'fill256_1', 'fill512_0', 'fill512_1'],
        eargs=[{'fillstyle':'left'},{'fillstyle':'right'},{'fillstyle':'left'},{'fillstyle':'right'}],
        ylim=0, l2pos=0.6, tweak=-1, base={'mew' : 0}, ms=10, uarch=icl)


if not args.noshow:
    plt.show()

