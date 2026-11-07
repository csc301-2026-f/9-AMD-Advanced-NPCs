import csv, hashlib, json, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
ROOT='/Users/olivia/Documents/Code/UofT/CSC301/9-AMD-Advanced-NPCs/Examples/AdaptiveTag'
S='/private/tmp/claude-501/-Users-olivia-Documents-Code-UofT-CSC301-9-AMD-Advanced-NPCs/6472e318-ae0f-4973-902c-79b5d0f527a9/scratchpad'
OUT=f'{S}/runs'; os.makedirs(OUT,exist_ok=True)
CHUNKS=int(os.environ.get('CHUNKS',24)); EPS=5; SEEDS=(1,2,3)
def build(flags):
    h=hashlib.md5(flags.encode()).hexdigest()[:8]; d=f'{S}/bins/{h}'
    if not os.path.exists(d+'/chase_sim'):
        env=dict(os.environ,EXTRA_FLAGS=flags)
        r=subprocess.run([f'{ROOT}/Tools/ChaseSim/Build-ChaseSim.sh',d],env=env,capture_output=True,text=True)
        if r.returncode: print('BUILD FAIL',flags,r.stderr[-500:]); sys.exit(1)
    return d+'/chase_sim'
def run(name, flags, args, seed, pattern='rightturn'):
    exe=build(flags); tag=f'{name}-s{seed}'; csv_path=f'{OUT}/{tag}.csv'
    script=f'{OUT}/{tag}.script'
    lines=[f'log {csv_path}',f'pattern {pattern}','eval 100']+[x for _ in range(CHUNKS) for x in (f'train {EPS}','eval 100')]+['quit']
    open(script,'w').write('\n'.join(lines)+'\n')
    import time; t=time.time()
    subprocess.run([exe,'--seed',str(seed)]+args.split()+['--script',script],stdin=subprocess.DEVNULL,capture_output=True)
    wall=time.time()-t
    rows=[r for r in csv.DictReader(open(csv_path)) if r['kind']=='eval' and r['policy']=='CURRENT']
    return [(int(r['total_steps']),float(r['catch_rate'])) for r in rows], wall
def at(curve,m):  # catch rate at the last checkpoint <= m moves
    v=[c for s,c in curve if s<=m]; return v[-1] if v else 0
def first(curve,th):
    for s,c in curve:
        if c>=th: return s
    return None
def summarize(name,res):
    curves=[r[0] for r in res]; wall=sum(r[1] for r in res)/len(res)
    c5=sum(at(c,5000) for c in curves)/3; c10=sum(at(c,10000) for c in curves)/3; c20=sum(at(c,20000) for c in curves)/3
    f=[first(c,0.6) for c in curves]; fs=[str(x) if x else '-' for x in f]
    last=sum(sum(x for _,x in c[-3:])/3 for c in curves)/3
    return f'{name:22s} 5k {c5:.2f} | 10k {c10:.2f} | 20k {c20:.2f} | last3 {last:.2f} | moves to 0.6: {",".join(fs):>20s} | wall/seed {wall:5.0f}s'
def sweep(variants, label):
    with ThreadPoolExecutor(6) as ex: list(ex.map(build, sorted({f for _,f,_ in variants})))
    jobs=[(n,f,a,s) for n,f,a in variants for s in SEEDS]
    with ThreadPoolExecutor(9) as ex: out=list(ex.map(lambda j: run(*j),jobs))
    lines=[]
    for i,(n,f,a) in enumerate(variants): lines.append(summarize(n,out[i*3:i*3+3]))
    txt='\n'.join(lines); print(txt); open(f'{S}/sweep-{label}.txt','w').write(txt+'\n')
if __name__=='__main__':
    which=sys.argv[1]
    V={}
    V['r1']=[('baseline','',''),
      ('utd2','','--updates_per_move 2'),('utd4','','--updates_per_move 4'),('utd8','','--updates_per_move 8'),('utd16','','--updates_per_move 16'),
      ('lr3e-4','-DONLINE_SAC_LR=0.0003',''),('lr3e-3','-DONLINE_SAC_LR=0.003',''),
      ('batch64','-DONLINE_SAC_BATCH=64',''),('batch128','-DONLINE_SAC_BATCH=128',''),('batch256','-DONLINE_SAC_BATCH=256',''),
      ('gamma0.9','-DONLINE_SAC_GAMMA=0.9',''),('gamma0.95','-DONLINE_SAC_GAMMA=0.95',''),('gamma0.98','-DONLINE_SAC_GAMMA=0.98',''),
      ('tau0.02','-DONLINE_SAC_TAU=0.02',''),('tau0.05','-DONLINE_SAC_TAU=0.05',''),
      ('hidden32','-DONLINE_SAC_HIDDEN=32',''),('hidden128','-DONLINE_SAC_HIDDEN=128',''),('hidden256','-DONLINE_SAC_HIDDEN=256',''),
      ('alpha0.1','-DONLINE_SAC_ALPHA=0.1',''),('alpha0.02','-DONLINE_SAC_ALPHA=0.02',''),
      ('replay5k','-DONLINE_SAC_REPLAY=5000',''),('replay20k','-DONLINE_SAC_REPLAY=20000',''),
      ('warmup100','','--warmup_experiences 100'),('warmup300','','--warmup_experiences 300'),
      ('bonus10','','--catch_bonus 10'),('bonus100','','--catch_bonus 100')]
    B='bonus10'; 
    def v(name, flags='', args='', bonus=10):
        return (name, flags, (args+f' --catch_bonus {bonus}').strip())
    REL='-DONLINE_SAC_RELATIVE_OBS'
    V['r2']=[v('bonus10'), v('bonus5',bonus=5), v('bonus20',bonus=20), v('rel',REL), v('h32','-DONLINE_SAC_HIDDEN=32'),
      v('utd8',args='--updates_per_move 8'), v('utd16',args='--updates_per_move 16'),
      v('h32+utd8','-DONLINE_SAC_HIDDEN=32','--updates_per_move 8'), v('h32+utd16','-DONLINE_SAC_HIDDEN=32','--updates_per_move 16'),
      v('h32+utd8+rel','-DONLINE_SAC_HIDDEN=32 '+REL,'--updates_per_move 8'),
      v('h32+utd8+lr3e-3','-DONLINE_SAC_HIDDEN=32 -DONLINE_SAC_LR=0.003','--updates_per_move 8'),
      v('h32+utd8+tau.05','-DONLINE_SAC_HIDDEN=32 -DONLINE_SAC_TAU=0.05','--updates_per_move 8'),
      v('h32+utd8+alpha.05','-DONLINE_SAC_HIDDEN=32 -DONLINE_SAC_ALPHA=0.05','--updates_per_move 8'),
      v('h32+utd8+warm100','-DONLINE_SAC_HIDDEN=32','--updates_per_move 8 --warmup_experiences 100'),
      v('ALL8','-DONLINE_SAC_HIDDEN=32 -DONLINE_SAC_LR=0.003 -DONLINE_SAC_TAU=0.05 -DONLINE_SAC_ALPHA=0.05 '+REL,'--updates_per_move 8 --warmup_experiences 100'),
      v('ALL16','-DONLINE_SAC_HIDDEN=32 -DONLINE_SAC_LR=0.003 -DONLINE_SAC_TAU=0.05 -DONLINE_SAC_ALPHA=0.05 '+REL,'--updates_per_move 16 --warmup_experiences 100')]
    REL2='-DONLINE_SAC_RELATIVE_OBS'
    V['r3']=[v('utd16+rel',REL2,'--updates_per_move 16'), v('utd32+h32','-DONLINE_SAC_HIDDEN=32','--updates_per_move 32')]
    sweep(V[which], which)
