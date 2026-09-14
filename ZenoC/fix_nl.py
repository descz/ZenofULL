import io

p = 'src/zenoc_cli.c'
t = io.open(p, encoding='utf-8').read()

bs_n = chr(92) + 'n'
broken = '"[auto-approve] %s %s' + chr(10) + '", tool_name'
fixed = '"[auto-approve] %s %s' + bs_n + '", tool_name'
assert t.count(broken) == 1
t = t.replace(broken, fixed, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('fixed')
