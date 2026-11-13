import sys, json, time; sys.path.insert(0, sys.argv[1]); from cdp import Chrome
c=Chrome(9330, sys.argv[2]+'/chrome'); log=open(sys.argv[2]+'/jt_frames.jsonl','w')
SEL={'StartJam':"$('[sbSet=\"StartJam\"]')",'StopJam':"$('[sbSet=\"StopJam\"]')",'Timeout':"$('.ClockButtons [sbSet=\"Timeout\"], [sbSet=\"Timeout\"].MainButton').last()",
     'TTO1':"$('.ToButtonRow.TTO [sbSet=\"Timeout\"]').eq(0)",'OR2':"$('.ToButtonRow.OR [sbSet=\"OfficialReview\"]').eq(1)",'OTO':"$('[sbSet=\"OfficialTimeout\"]')",
     'Undo':"$('[sbSet=\"ClockReplace\"]')",'PeriodMinus':"$('[sbSet=\"Time: -1000: change\"]').eq(0)"}
def summ(m):
    try: st=json.loads(m).get('state')
    except Exception: return m
    if st is None: return m
    keys={k.replace('ScoreBoard.CurrentGame.','G.'):v for k,v in st.items()}
    t=[k for k in keys if k.endswith('.Time')]
    rest={k:v for k,v in keys.items() if not k.endswith('.Time') and not k.endswith('InvertedTime')}
    return json.dumps(rest)[:900] + (f"  +{len(t)} Time keys: " + ", ".join(f"{k[2:-5]}={keys[k]}" for k in t) if t else "")
try:
    p=c.page(); p.go('http://127.0.0.1:4330/nso/jt/', 5)
    def step(name, wait=2.5, then=None):
        i=len(p.frames); t0=time.time()
        r=p.ev(SEL[name]+".length+':'+"+SEL[name]+".text().trim()+':'+"+SEL[name]+".is(':visible')")
        p.ev(SEL[name]+".click()")
        if then: p.wait(then[1]); p.ev(SEL[then[0]]+".click()")
        p.wait(wait)
        print(f"\n### {name}{' + '+then[0]+' after '+str(then[1])+'s' if then else ''}  [{r}]")
        first=None
        for t,d,m in p.frames[i:]:
            log.write(json.dumps({'step':name,'t':round(t-t0,3),'dir':d,'msg':m})+'\n')
            if d=='sent': print(f"  {t-t0:6.3f} SENT {m}")
            else:
                if first is None: first=t-t0
                print(f"  {t-t0:6.3f} recv {summ(m)}")
    step('StopJam')                 # Label(Stop)=Lineup: start lineup
    step('StartJam', 3.2)
    step('StopJam')
    step('Timeout')
    step('TTO1')
    step('StopJam')                 # end timeout
    step('Undo', 2.0)               # ClockReplace
    step('StartJam', 3.0)           # replaces the undone action?
    step('StopJam', 2.0, then=('StartJam',0.4))   # different button within 1 s
    step('StopJam', 2.0, then=('StopJam',0.3))    # same button twice within 1 s
    step('Timeout', 2.0)
    step('OR2', 2.0)
    step('OTO', 2.0)
    step('PeriodMinus', 1.5)
    print('errors', p.errs[:3]); p.close()
finally: c.quit()
