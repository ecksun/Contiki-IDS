TIMEOUT(600);

while (true) {
  log.log(time + " ID:" + id + ": " + msg + "\n");
  YIELD();
}
