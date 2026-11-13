import websocket, json, time
URL="ws://localhost:4330/WS/?source=jamtimer&platform=probe"
cookie=open('cookie.txt').read().strip()
ws=websocket.create_connection(URL, header=[f"Cookie: CRG_SCOREBOARD={cookie}"], suppress_origin=True, timeout=3)
st={}
def drain(t):
    out=[]; end=time.time()+t
    while time.time()<end:
        ws.settimeout(max(0.02,end-time.time()))
        try: m=json.loads(ws.recv())
        except Exception: break
        out.append(m); st.update(m.get('state',{}))
    return out
dev=json.loads(ws.recv())['state']['WS.Device.Id']
G='ScoreBoard.CurrentGame.'
ws.send(json.dumps({"action":"Register","paths":[G+"Clock(*).Running",G+"Clock(*).Time",G+"Clock(*).Number",G+"InJam",G+"Label(*)",G+"TimeoutOwner",G+"Team(*).Id",G+"Game","ScoreBoard.Settings.Setting(ScoreBoard.Clock.Sync)",f"ScoreBoard.Clients.Device({dev}).MayWrite","ScoreBoard.Settings.Setting(ScoreBoard.Clients.NewDeviceWrite)","ScoreBoard.Clients.NewDeviceWrite","ScoreBoard.Clients.AllLocalDevicesWrite"]}))
drain(1)
def cmd(c): ws.send(json.dumps({"action":"Set","key":G+c,"value":True,"flag":""}))
def show(label):
    run=[k[len(G)+6:-9] for k,v in st.items() if k.endswith('.Running') and v]
    print(f"  {label}: running={run} InJam={st.get(G+'InJam')} jam#={st.get(G+'Clock(Jam).Number')} TO#={st.get(G+'Clock(Timeout).Number')} Stop={st.get(G+'Label(Stop)')!r} Undo={st.get(G+'Label(Undo)')!r} JamTime={st.get(G+'Clock(Jam).Time')} TOTime={st.get(G+'Clock(Timeout).Time')}")
print("team ids:", {k:v for k,v in st.items() if k.endswith('.Id') and 'Team' in k}, "game:", st.get(G+'Game'))
print("client settings:", {k:v for k,v in st.items() if 'Clients' in k or 'Sync' in k})
def seq(name, first, second, gap):
    # get to a clean lineup first
    for c in ('StopJam',):
        cmd(c); drain(1.3)
    if st.get(G+'InJam') or st.get(G+'Clock(Timeout).Running'): cmd('StopJam'); drain(1.3)
    if not st.get(G+'Clock(Lineup).Running'): cmd('StopJam'); drain(1.3)
    if first=='StopJam': cmd('StartJam'); drain(2.5)
    print(f"== {name}: {first}, then {second} after {gap}s"); show("before")
    cmd(first); drain(gap); show(f"after {first}")
    cmd(second); drain(2.0); show(f"after {second} (+2s)")
seq("start then timeout (replace expected)", 'StartJam','Timeout',0.4)
seq("start then stop (ignored per source)", 'StartJam','StopJam',0.4)
seq("stop then start (ignored per source)", 'StopJam','StartJam',0.4)
seq("start then start (same button)", 'StartJam','StartJam',0.4)
seq("stop then timeout (replace expected)", 'StopJam','Timeout',0.4)
seq("start then timeout after 1.2s (no guard)", 'StartJam','Timeout',1.2)
# write permission
print("== write permission")
ws.send(json.dumps({"action":"Set","key":f"ScoreBoard.Clients.Device({dev}).MayWrite","value":False,"flag":""})); print("  ", drain(0.5))
cmd('StopJam'); print("   Set while not allowed ->", drain(0.6))
ws.send('{"action":"Ping"}'); print("   ping ->", drain(0.3))
ws.close()
# re-enable with a second (fresh, local) client: AllLocalDevicesWrite?
w2=websocket.create_connection(URL, suppress_origin=True, timeout=3); json.loads(w2.recv())
w2.send(json.dumps({"action":"Set","key":f"ScoreBoard.Clients.Device({dev}).MayWrite","value":True,"flag":""})); time.sleep(0.5)
w2.send(json.dumps({"action":"Register","paths":[f"ScoreBoard.Clients.Device({dev}).MayWrite"]})); time.sleep(0.5); print("   re-enabled by another device:", w2.recv()); w2.close()
