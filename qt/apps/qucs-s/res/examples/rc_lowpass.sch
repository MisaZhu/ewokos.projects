<Qucs Schematic 0.0.19>
<Properties>
  View=0,0,800,600,1,0,0
  gridSize=10
  showFrame=0
</Properties>
<Symbol>
</Symbol>
<Components>
  <Vac V1 1 100 160 1 0 "1 V" 1 "0" 0 "1 kHz" 0 "0" 0>
  <R R1 1 160 120 0 0 "1 kOhm" 1>
  <C C1 1 180 160 1 0 "100 nF" 1 "0 V" 0>
  <GND * 1 100 220 0 0>
  <GND * 1 180 220 0 0>
  <AC AC1 1 60 60 0 0 "log" 0 "10 Hz" 0 "10 MHz" 0 "201" 0>
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
    logX=1
    logY=1
    plot=1
  </Rect>
</Diagrams>
<Paintings>
</Paintings>
