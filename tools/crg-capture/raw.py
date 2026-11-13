import websocket, json, time, sys
URL="ws://localhost:4330/WS/?source=jamtimer&platform=ESP32-C6%20fw0.1"
def conn(cookie=None):
    h=[f"Cookie: CRG_SCOREBOARD={cookie}"] if cookie else []
    ws=websocket.create_connection(URL, header=h, suppress_origin=True, timeout=3)
    sc=[v for k,v in ws.getheaders().items() if k.lower()=="set-cookie"]
    return ws, sc
def drain(ws, t=1.0, show=True, limit=600):
    out=[]; end=time.time()+t
    while time.time()<end:
        ws.settimeout(max(0.05,end-time.time()))
        try: m=ws.recv()
        except Exception: break
        out.append(m)
        if show: print("  <-", m[:limit] + ("..." if len(m)>limit else ""), f"[{len(m)} B]")
    return out
print("== 1. connect without cookie")
ws,sc=conn(); print("  upgrade Set-Cookie:", sc)
fr=drain(ws,0.8)
dev=json.loads(fr[0])["state"]["WS.Device.Id"]
cookie=sc[0].split(";")[0].split("=",1)[1] if sc else None
print("== 2. Ping"); ws.send('{"action":"Ping"}'); drain(ws,0.5)
print("== 3. Register minimal list")
paths=["ScoreBoard.CurrentGame.Clock(*).Time","ScoreBoard.CurrentGame.Clock(*).Running","ScoreBoard.CurrentGame.Clock(*).Direction","ScoreBoard.CurrentGame.Clock(*).Number","ScoreBoard.CurrentGame.InJam","ScoreBoard.CurrentGame.NoMoreJam","ScoreBoard.CurrentGame.OfficialScore","ScoreBoard.CurrentGame.TimeoutOwner","ScoreBoard.CurrentGame.OfficialReview","ScoreBoard.CurrentGame.FiveSeconds","ScoreBoard.CurrentGame.Label(*)",f"ScoreBoard.Clients.Device({dev}).MayWrite"]
ws.send(json.dumps({"action":"Register","paths":paths})); fr=drain(ws,1.0,limit=3000)
print("== 4. unknown action"); ws.send('{"action":"Bogus"}'); drain(ws,0.5)
ws.close()
print("== 5. reconnect WITH cookie")
ws,sc=conn(cookie); print("  upgrade Set-Cookie:", sc); fr=drain(ws,0.5)
print("  same device:", json.loads(fr[0])["state"]["WS.Device.Id"]==dev); ws.close()
print("== 6. reconnect WITHOUT cookie")
ws,sc=conn(); fr=drain(ws,0.5); print("  same device:", json.loads(fr[0])["state"]["WS.Device.Id"]==dev); ws.close()
open(sys.argv[1],"w").write(cookie or "")
