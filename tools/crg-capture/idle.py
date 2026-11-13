import websocket, time
w=websocket.create_connection("ws://localhost:4330/WS/?source=idle", suppress_origin=True, timeout=400); w.recv(); t0=time.time()
try:
    while True:
        op,data=w.recv_data(control_frame=True)
        if op==websocket.ABNF.OPCODE_CLOSE: print(f"closed by server after {time.time()-t0:.0f} s, close frame {data!r}"); break
        print(f"{time.time()-t0:.0f}s frame op={op} {data[:80]!r}")
except Exception as e: print(f"ended after {time.time()-t0:.0f} s: {type(e).__name__} {e}")
