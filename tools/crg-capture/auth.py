import websocket, json, time
URL="ws://localhost:4330/WS/?source=jamtimer&platform=probe"
cookie=open('cookie.txt').read().strip()
def c(ck=None):
    w=websocket.create_connection(URL, header=[f"Cookie: CRG_SCOREBOARD={ck}"] if ck else [], suppress_origin=True, timeout=3)
    return w, json.loads(w.recv())['state']['WS.Device.Id']
def drain(w,t):
    out=[]; end=time.time()+t
    while time.time()<end:
        w.settimeout(max(0.02,end-time.time()))
        try: out.append(w.recv())
        except Exception: break
    return out
def S(w,k,v): w.send(json.dumps({"action":"Set","key":k,"value":v,"flag":""}))
admin,aid=c()
dev_ws,dev=c(cookie)
dev_ws.send(json.dumps({"action":"Register","paths":[f"ScoreBoard.Clients.Device({dev}).MayWrite","ScoreBoard.CurrentGame.Label(Stop)"]})); drain(dev_ws,0.5)
S(admin,"ScoreBoard.Clients.AllLocalDevicesWrite",False); S(admin,f"ScoreBoard.Clients.Device({dev}).MayWrite",False)
print("dev sees:", drain(dev_ws,0.6))
S(dev_ws,"ScoreBoard.CurrentGame.Timeout",True); print("Set Timeout while refused ->", drain(dev_ws,0.8))
dev_ws.send(json.dumps({"action":"Register","paths":["ScoreBoard.CurrentGame.InJam"]})); print("Register while refused ->", drain(dev_ws,0.5))
# a brand-new device while NewDeviceWrite is true, from a non-local view: (still local here) check its MayWrite
n,nid=c(); n.send(json.dumps({"action":"Register","paths":[f"ScoreBoard.Clients.Device({nid}).MayWrite"]})); print("new device MayWrite:", drain(n,0.5)); n.close()
S(admin,f"ScoreBoard.Clients.Device({dev}).MayWrite",True); S(admin,"ScoreBoard.Clients.AllLocalDevicesWrite",True)
print("restored:", drain(dev_ws,0.6)); dev_ws.close(); admin.close()
