# Put useful information here

# wav to raw

ffmpeg -i CP.WAV -f f32le -acodec pcm_f32le output.raw

# 808 and other samples as WAV

https://github.com/tidalcycles/Dirt-Samples/blob/master/808/TR808.TXT

```elixir
port = Port.open({:spawn, "./exaudio"}, [:binary])
Port.command(port, :erlang.term_to_binary({"scan"}))
r = receive do msg -> msg end
{_, {:data, s}} = r
:erlang.binary_to_term s
Port.close()
```

```elixir
port = Port.open({:spawn, "./exaudio"}, [:binary])
Port.command(port, :erlang.term_to_binary({"log-on"}))
Port.command(port, :erlang.term_to_binary({"scan"}))
{_, {:data, s}} = receive do msg -> msg end
:erlang.binary_to_term s
```
