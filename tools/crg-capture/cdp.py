# Small DevTools helper: headless Chrome, one page, all WebSocket frames logged with times.
import json, time, subprocess, urllib.request, websocket, os
class Chrome:
    def __init__(self, dbg, profile):
        self.dbg=dbg
        self.p=subprocess.Popen(['google-chrome','--headless=new','--no-sandbox','--disable-gpu',f'--remote-debugging-port={dbg}',f'--user-data-dir={profile}','--window-size=1200,900','about:blank'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        for i in range(60):
            try: urllib.request.urlopen(f'http://127.0.0.1:{dbg}/json/version',timeout=1); return
            except Exception: time.sleep(.5)
    def page(self): return Page(self)
    def quit(self): self.p.terminate(); self.p.wait()
class Page:
    def __init__(self, c):
        self.c=c
        self.t=json.load(urllib.request.urlopen(urllib.request.Request(f'http://127.0.0.1:{c.dbg}/json/new?about:blank',method='PUT')))
        self.ws=websocket.create_connection(self.t['webSocketDebuggerUrl'],suppress_origin=True,timeout=60)
        self.n=0; self.frames=[]; self.errs=[]; self.hs=[]
        self.cmd('Network.enable'); self.cmd('Runtime.enable')
    def handle(self,m):
        meth=m.get('method',''); p=m.get('params',{})
        if meth in('Network.webSocketFrameSent','Network.webSocketFrameReceived'):
            self.frames.append((time.time(), 'sent' if meth.endswith('Sent') else 'recv', p['response']['payloadData']))
        elif meth=='Network.webSocketWillSendHandshakeRequest': self.hs.append(('req',p['request'].get('headers',{})))
        elif meth=='Network.webSocketHandshakeResponseReceived': self.hs.append(('resp',p['response'].get('headers',{})))
        elif meth=='Network.webSocketCreated': self.hs.append(('url',p.get('url')))
        elif meth=='Runtime.exceptionThrown': self.errs.append(str(p['exceptionDetails'])[:300])
    def cmd(self,meth,**p):
        self.n+=1; self.ws.send(json.dumps({'id':self.n,'method':meth,'params':p}))
        while True:
            m=json.loads(self.ws.recv())
            if m.get('id')==self.n: return m.get('result',{})
            self.handle(m)
    def wait(self,s):
        self.ws.settimeout(0.1); t0=time.time()
        while time.time()-t0<s:
            try: self.handle(json.loads(self.ws.recv()))
            except websocket.WebSocketTimeoutException: pass
        self.ws.settimeout(60)
    def ev(self,x):
        r=self.cmd('Runtime.evaluate',expression=x,returnByValue=True)
        return 'JSERR '+str(r['exceptionDetails'])[:300] if 'exceptionDetails' in r else r.get('result',{}).get('value')
    def go(self,url,s=5): self.cmd('Page.navigate',url=url); self.wait(s)
    def close(self):
        self.ws.close(); urllib.request.urlopen(urllib.request.Request(f'http://127.0.0.1:{self.c.dbg}/json/close/'+self.t['id']))
