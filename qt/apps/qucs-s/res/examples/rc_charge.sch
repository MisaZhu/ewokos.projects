<Qucs Schematic 0.0.19>
<Properties>
  View=0,0,800,600,1,0,0
  gridSize=10
  showFrame=0
</Properties>
<Symbol>
</Symbol>
<Components>
  <Vpulse V1 1 100 160 1 0 "0 V" 1 "5 V" 0 "0 s" 0 "1 ns" 0 "10 ms" 0 "1 ns" 0 "100 ms" 0>
  <R R1 1 160 120 0 0 "1 kOhm" 1>
  <C C1 1 180 160 1 0 "1 uF" 1 "0 V" 0>
  <GND * 1 100 220 0 0>
  <GND * 1 180 220 0 0>
  <Tran Tran1 1 60 60 0 0 "0 s" 0 "20 ms" 0 "501" 0>
</Components>
<Wires>
  <100 140 100 120 "" 0 0 0>
  <100 120 140 120 "" 0 0 0>
  <180 120 180 140 "out" 0 0 0>
  <100 180 100 200 "" 0 0 0>
  <180 180 180 200 "" 0 0 0>
</Wires>
<Diagrams>
  <Rect 320 80 420 280>
    vars="out"
    logX=0
    logY=0
    plot=0
  </Rect>
</Diagrams>
<Paintings>
</Paintings>
