# 生成 GB2312 一级字库（3755 常用字）+ 常用标点的字符列表
# 供 lv_font_conv 生成全量中文字体用。输出到 tools/gb2312_chars.txt
#
# 用法: python gen_gb2312_chars.py

chars = []
# GB2312 一级字库：区 16-55，每区 94 个汉字（拼音序）
for qu in range(16, 56):
    for wei in range(1, 95):
        code = bytes([qu + 0xA0, wei + 0xA0])
        try:
            chars.append(code.decode('gb2312'))
        except Exception:
            pass

# 常用中文标点 + 数字/字母区间由 lv_font_conv 的 -r 参数单独加
extra = '，。！？；：、（）【】“”‘’《》—…·％℃'
for ch in extra:
    if ch not in chars:
        chars.append(ch)

result = ''.join(chars)
with open('tools/gb2312_chars.txt', 'w', encoding='utf-8') as f:
    f.write(result)

print('生成字符数:', len(chars))
print('样例:', result[:60])
