const bedrock = require("bedrock-protocol");

const relay = new bedrock.Relay({
  host: "127.0.0.1",
  port: 19133,
  destination: {
    host: "127.0.0.1",
    port: 19132,
  },
});

const excluded = ["player_auth_input", "move_player"];

relay.on("connect", (c) => {
  console.log("新的连接。");
  c.on("clientbound", ({ name, param }) => {
    if (excluded.includes(name)) return;
    console.log(name, param);
  });
});

relay.listen();
