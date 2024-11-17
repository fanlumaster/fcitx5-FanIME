# 燕返

也可以叫燕归来。可以理解为一式剑招，也可以理解成一种春天的意境，归结下来，打字，当如佐佐木小次郎的剑招一般，快如闪电，可斩飞燕，然而斩燕毕竟过于残忍，所以，也可以想象成燕子归来的意境，打字如春风拂面，燕子回返，回旋于天际，不落痕迹记录敏思。

总结：一个输入法。

## 简要介绍

This is a fcitx5-based input method, meant to improve the input Chinese experience in Linux.

Demo video: 

- <https://www.bilibili.com/video/BV12ty1Y4Esu> 
- <https://www.bilibili.com/video/BV1kvy1YQECp>

### 一些使用上的说明

1、目前只为完整的双拼提供组词功能，比如，

luziyy => 分词: lu'zi'yy => 卢子云

但是，像下面这种的组词是不被允许的，

jjjjj => 分词: j'j'j'j'j !!!no!!!

2、默认支持一个单码辅助码支持。

3、按 Tab 键开启完整的辅助码支持，不过，只支持单字和双字的完整的辅助码，因为三个字以及三个字以上的词语的重复率非常低。

## How to compile and install manually

First, install required libs,

```bash
yay -S boost
```

Then, create a directory for the use of storing db file and log file,

```bash
mkdir ~/.local/share/fcitx5-fanime
```

Then, clone my repo of this to another directory in your PC: <https://github.com/fanlumaster/FanyDictForIME.git>，and run the following commands to generate database,

```bash
git clone https://github.com/fanlumaster/FanyDictForIME.git
cd ./makecikudb/xnheulpb/makedb/separated_jp_version
python ./create_db_and_table.py
python ./insert_data.py
python ./create_index_for_db.py
cd ./out
cp ./cutted_flyciku_with_jp.db ~/.local/share/fcitx5-fanime/
```

Then, return back to this project,

```bash
cp ./assets/pinyin.txt ~/.local/share/fcitx5-fanime/
cp ./assets/helpcode.txt ~/.local/share/fcitx5-fanime/
./lcompile.sh
```

Then, restart fcitx5, and add fcitx5-fanime, and you could type Chinese words with this IME now.

## 感谢

- <https://github.com/fcitx/fcitx5>
- <https://github.com/fcitx/fcitx5-quwei>
- <https://github.com/fcitx/fcitx5-chinese-addons>


