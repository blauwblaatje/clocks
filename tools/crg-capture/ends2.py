import websocket, json, time
w=websocket.create_connection("ws://localhost:4330/WS/?source=jamtimer&platform=probe", suppress_origin=True, timeout=3); w.recv()
G='ScoreBoard.CurrentGame.'; st={}
def drain(t):
    end=time.time()+t
    while time.time()<end:
        w.settimeout(max(0.02,end-time.time()))
        try: st.update(json.loads(w.recv()).get('state',{}))
        except Exception: break
w.send(json.dumps({"action":"Register","paths":[G+"Clock(*).Running",G+"Clock(*).Time",G+"Clock(*).Number",G+"Clock(*).Name",G+"Clock(*).MaximumTime",G+"InJam",G+"InPeriod",G+"InOvertime",G+"NoMoreJam",G+"Label(*)",G+"OfficialScore",G+"Team(*).Score"]})); drain(1)
def cmd(c,v=True,flag="",t=1.3): w.send(json.dumps({"action":"Set","key":G+c,"value":v,"flag":flag})); drain(t)
g=lambda k: st.get(G+k)
def show(label):
    run=[k[len(G)+6:-9] for k,v in st.items() if k.endswith('.Running') and v and 'Clock(' in k]
    print(f"{label:38} run={run} P#={g('Clock(Period).Number')} P={g('Clock(Period).Time')} J#={g('Clock(Jam).Number')} InPeriod={g('InPeriod')} NoMore={g('NoMoreJam')} OT={g('InOvertime')} Int={g('Clock(Intermission).Name')!r}/{g('Clock(Intermission).Time')} Off={g('OfficialScore')} | Start={g('Label(Start)')!r} Stop={g('Label(Stop)')!r}")
def end_period(tag):
    if not g('InJam'): cmd('StartJam')
    cmd('Clock(Period).Time', 1500-g('Clock(Period).Time'), 'change', 2.5)   # set to 1.5 s, let it run out
    show(f"{tag}: period clock out, jam on"); cmd('StopJam'); show(f"{tag}: jam stopped")
show("start")
end_period("P1")
ic=g('Clock(Intermission).Time'); cmd('Clock(Intermission).Time', -(ic-5000), 'change'); show("P1 intermission, 5 s left")
cmd('StartJam'); show("crown in late intermission")
end_period("P2")
cmd('StartJam'); show("crown after last period, tied")
if g('InJam'): cmd('ClockUndo'); show("undo that")
cmd('StopJam'); show("StopJam (Overtime Lineup?)")
cmd('StartJam'); show("crown in OT lineup")
w.close()
