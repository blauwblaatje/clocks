import sys, json; sys.path.insert(0, sys.argv[1]); from cdp import Chrome
c=Chrome(9330, sys.argv[2]+'/chrome')
try:
    p=c.page(); p.go('http://127.0.0.1:4330/nso/jt/', 6)
    for kind,h in p.hs: print(kind, h if kind!='resp' else {k:v for k,v in h.items() if k.lower() in('set-cookie',)})
    for t,d,m in p.frames:
        if d=='sent': print('SENT', m[:4000])
    print('recv frames', sum(1 for f in p.frames if f[1]=='recv'), 'bytes', sum(len(f[2]) for f in p.frames if f[1]=='recv'))
    print(p.ev("""JSON.stringify($('[sbSet],[sbControl]').map(function(){return [this.tagName, $(this).attr('sbSet')||$(this).attr('sbControl'), $(this).text().trim().slice(0,20), $(this).is(':visible')]}).get())"""))
    print('errors', p.errs[:3]); p.close()
finally: c.quit()
