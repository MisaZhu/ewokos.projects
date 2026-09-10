<Qucs Schematic 0.0.19>
<Properties>
  View=0,0,800,600,1,0,0
  gridSize=10
  showFrame=0
</Properties>
<Symbol>
</Symbol>
<Components>
  <Vdc V1 1 100 160 1 0 "1 V" 1>
  <Iprobe Pr1 1 140 120 0 0>
  <R R1 1 200 120 0 0 "100 Ohm" 1>
  <Diode D1 1 260 120 0 0 "1e-15 A" 0 "1" 0 "0 Ohm" 0 "0 F" 0 "0.7 V" 0 "0.5" 0>
  <GND * 1 100 220 0 0>
  <GND * 1 300 220 0 0>
  <DC DC1 1 60 60 0 0 "V1" 0 "0 V" 0 "1 V" 0 "0.02 V" 0>
</Components>
<Wires>
  <100 140 100 120 "" 0 0 0>
  <100 120 120 120 "" 0 0 0>
  <160 120 180 120 "" 0 0 0>
  <220 120 240 120 "" 0 0 0>
  <280 120 300 120 "" 0 0 0>
  <300 120 300 200 "" 0 0 0>
  <100 180 100 200 "" 0 0 0>
</Wires>
<Diagrams>
  <Rect 360 80 420 280>
    vars="Pr1.I"
    logX=0
    logY=0
    plot=0
  </Rect>
</Diagrams>
<Paintings>
</Paintings>
