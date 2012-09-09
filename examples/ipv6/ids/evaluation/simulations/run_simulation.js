TIMEOUT(1900000);

sim.setSpeedLimit(1.0);

while (true) {
  log.log(time + " ID:" + id + ": " + msg + "\n");
  YIELD();
}
