# Put useful information here

# wav to raw

ffmpeg -i CP.WAV -f f32le -acodec pcm_f32le output.raw

# 808 and other samples as WAV

https://github.com/tidalcycles/Dirt-Samples/blob/master/808/TR808.TXT

```elixir
p = Port.open({:spawn, "./exaudio"}, [:binary])
Port.command(p, :erlang.term_to_binary({"scan"}))
r = receive do msg -> msg end
{_, {:data, s}} = r
:erlang.binary_to_term s
Port.close(p)
```

```elixir
p = Port.open({:spawn, "./exaudio"}, [:binary])
Port.command(p, :erlang.term_to_binary({"log-on"}))
Port.command(p, :erlang.term_to_binary({"scan"}))
{_, {:data, s}} = receive do msg -> msg end
:erlang.binary_to_term s
```

```bash
#!/bin/bash
# tobin 131 
# this turns a series of base10 numbers to a binary string on stdout
[ "$1" = "" ] && exit
for i in $*; do
  echo $i | awk '{a=sprintf("%s%c", a, $1+0);}END{printf("%s", a);}'
done
```

```bash
#!/bin/bash

# this is {}
./tobin 131 104 000 | socat - UDP4-DATAGRAM:127.0.0.1:12345

# this is {"scan"}
./tobin 131 104 001 109 000 000 000 004 115 99 97 110 | socat - UDP4-DATAGRAM:127.0.0.1:12345
```

```bash
#!/bin/bash
socat -u UDP4-RECV:12345 STDOUT | ./exaudio
```