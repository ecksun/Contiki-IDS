radiomedium = mote.getSimulation().getRadioMedium();
log.log("dgrm : " + radiomedium + "\n");

edges = radiomedium.getEdges();
var edge = new Array(2);
var i = 0;
for (var key in edges) {
  if ((edges[key].source.getMote().getID() == 2 && edges[key].superDest.radio.getMote().getID() == 25) ||
     (edges[key].source.getMote().getID() == 25 && edges[key].superDest.radio.getMote().getID() == 2)) {
       edge[i++] = edges[key]; 
     }
}

while (true) {
  if (edge[0].superDest.ratio == 0) {
    edge[0].superDest.ratio = 1;
    edge[1].superDest.ratio = 1;
  }
  else {
    edge[0].superDest.ratio = 0;
    edge[1].superDest.ratio = 0;
  }
  radiomedium.requestEdgeAnalysis();

  GENERATE_MSG(1000, "wormhole_toggle");
  YIELD_THEN_WAIT_UNTIL(msg.equals("wormhole_toggle"));

}
