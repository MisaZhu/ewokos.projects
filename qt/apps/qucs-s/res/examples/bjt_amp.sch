<Qucs Schematic 0.0.19>
<Properties>
  View=0,0,800,600,1,0,0
  gridSize=10
  showFrame=0
</Properties>
<Symbol>
</Symbol>
<Components>
  <Vdc V2 1 260 60 1 0 "12 V" 1>
  <GND * 1 260 20 2 0>
  <R Rc 1 260 120 1 0 "1 kOhm" 1>
  <BJT Q1 1 250 180 0 0 "npn" 0 "1e-16 A" 0 "100" 0 "1" 0>
  <R Rb 1 190 180 0 0 "100 kOhm" 1>
  <Vdc V1 1 130 180 0 0 "1 V" 1>
  <GND * 1 90 220 0 0>
  <GND * 1 260 240 0 0>
  <DC DC1 1 60 60 0 0 "V1" 0 "0 V" 0 "2 V" 0 "0.05 V" 0>
</Components>
<Wires>
  <260 80 260 100 "" 0 0 0>
  <260 140 260 160 "c" 0 0 0>
  <260 200 260 220 "" 0 0 0>
  <210 180 230 180 "" 0 0 0>
  <150 180 170 180 "" 0 0 0>
  <110 180 90 180 "" 0 0 0>
  <90 180 90 200 "" 0 0 0>
</Wires>
<Diagrams>
  <Rect 360 80 420 280>
    vars="c"
    logX=0
    logY=0
    plot=0
  </Rect>
</Diagrams>
<Paintings>
</Paintings>
