import sys, json; sys.path.insert(0, sys.argv[1]); from cdp import Chrome
c=Chrome(9331, sys.argv[2]+'/chrome2')
try:
    p=c.page(); p.go('http://127.0.0.1:4330/nso/sbo/?game=1aef6b10-ad1f-4aeb-b5d0-4b6a91bc89e3&operator=probe', 8)
    print("url:", [h for k,h in p.hs if k=='url'])
    regs=[m for t,d,m in p.frames if d=='sent' and 'Register' in m]
    paths=[x for m in regs for x in json.loads(m)['paths']]
    print("register frames", len(regs), "paths", len(paths), "unique", len(set(paths)))
    print("recv bytes on load", sum(len(m) for t,d,m in p.frames if d=='recv'))
    print("visible clock buttons:", p.ev("JSON.stringify($('#StartJam,#StopJam,#Timeout,#ClockUndo').map(function(){return this.id+'='+$(this).text().trim()+':'+$(this).is(':visible')}).get())"))
    for b,w in (('StopJam',1.5),('StartJam',1.5),('StopJam',1.5),('Timeout',1.5),('ClockUndo',1.5)):
        i=len(p.frames); p.ev(f"$('#{b}').click()"); p.wait(w)
        print(b, "->", [m for t,d,m in p.frames[i:] if d=='sent' and 'Ping' not in m])
    print('errors', p.errs[:3]); p.close()
finally: c.quit()
