# ds18b20

运行于树莓派上的温度采集程序，支持将数据写入 `sqlite` 和 `influxdb`。

# 依赖
* boost
* curl
* rapidjson
* sqlite3

# 编译
```bash
make -C src/w1_therm release
```

# 运行
```bash
w1_therm -n switch -p path/to/w1_slave -c path/to/config -d
```

|**Option**|**Description**|
|-|-|
|`-n`|采集对象的名称|
|`-p`|`w1_slave` 的路径|
|`-c`|配置文件|
|`-d`|以守护进程运行|