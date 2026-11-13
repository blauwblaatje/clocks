import websocket, json, time
w=websocket.create_connection("ws://localhost:4330/WS/?source=jamtimer&platform=probe", suppress_origin=True, timeout=3); w.recv()
G='ScoreBoard.CurrentGame.'; st={}
def drain(t):
    end=time.time()+t
    while time.time()<end:
        w.settimeout(max(0.02,end-time.time()))
        try: st.update(json.loads(w.recv()).get('state',{}))
        except Exception: break
w.send(json.dumps({"action":"Register","paths":[G+"Clock(*).Running",G+"Clock(*).Time",G+"Clock(*).Number",G+"Clock(*).Name",G+"InJam",G+"InPeriod",G+"InOvertime",G+"NoMoreJam",G+"Label(*)",G+"OfficialScore",G+"Team(*).Score",G+"TimeoutOwner"]})); drain(1)
def cmd(c,v=True,flag=""): w.send(json.dumps({"action":"Set","key":G+c,"value":v,"flag":flag})); drain(1.3)
def show(label):
    run=[k[len(G)+6:-9] for k,v in st.items() if k.endswith('.Running') and v and 'Clock(' in k]
    g=lambda k: st.get(G+k)
    print(f"{label:34} run={run} P#={g('Clock(Period).Number')} P={g('Clock(Period).Time')} J#={g('Clock(Jam).Number')} InPeriod={g('InPeriod')} NoMoreJam={g('NoMoreJam')} OT={g('InOvertime')} Int={g('Clock(Intermission).Name')}/{g('Clock(Intermission).Time')} | Start={g('Label(Start)')!r} Stop={g('Label(Stop)')!r} TO={g('Label(Timeout)')!r} score={g('Team(1).Score')}-{g('Team(2).Score')}")
show("start")
for i in range(2):
    if st.get(G+'Label(Stop)')=='End Timeout': cmd('StopJam')
    if st.get(G+'InJam'): cmd('StopJam')
    cmd('StartJam'); show(f"P{i+1}: jam running")
    p=st.get(G+'Clock(Period).Time'); cmd('Clock(Period).Time', -(p-2000), 'change'); drain(2.5); show(f"P{i+1}: period clock out, in jam")
    cmd('StopJam'); show(f"P{i+1}: jam stopped after period end")
    drain(1); show(f"P{i+1}: +1 s")
    if i==0:
        cmd('StartJam'); show("P1 over: crown StartJam")
        if st.get(G+'InJam'): cmd('StopJam')
w.close()
