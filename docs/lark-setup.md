# Lark(海外版)侧配置指南

设备直连 Lark 开放平台读取多维表格(Base),需要一次性完成以下配置(约 10 分钟)。

> 本项目对接 **Lark 国际版**(larksuite.com)。固件 API 域名为 `open.larksuite.com`,
> 如果你的组织用的是中国版飞书(feishu.cn),需把 `lark_client.cpp` 中的域名改为 `open.feishu.cn`(两版根证书均已内置)。

## 1. 创建多维表格(Base)

在 Lark 中新建一个 **Base**(多维表格),建一张数据表(如命名"菜单"),字段严格按下表设置(字段名要与固件 `secrets.h` 中一致,默认如下):

| 字段名 | 字段类型 | 说明 |
|---|---|---|
| 日期 | 日期 Date | 每天一行,用日期选择器填 |
| 早餐 | 多行文本 Text | 一行一道菜 |
| 午餐 | 多行文本 Text | 一行一道菜 |
| 晚餐 | 多行文本 Text | 一行一道菜 |
| 备注 | 文本 Text(可选) | 如"少辣",显示在屏幕左下角 |

> 列名可以用英文(如 Date/Breakfast…),只要与 `secrets.h` 里的 `LARK_FIELD_*` 一致即可。

日常使用:
- **填次日菜单**:新增一行,日期选明天
- **更新当日菜单**:直接改今天那一行,设备最迟在下一个同步窗口生效;想立即生效就**按住设备上的 KEY 键约 1.5 秒**

## 2. 创建自建应用(Custom App)

1. 打开 [Lark 开放平台](https://open.larksuite.com/) → Developer Console → **Create Custom App**(企业自建应用),名称随意(如"Menu Board")
2. 进入应用 → **Permissions & Scopes** → 搜索并开通:
   - `bitable:app:readonly`(View Base)
3. **Version Management & Release** → 创建版本并发布(自建应用一般自动/管理员审核通过)
4. 记录 **Credentials & Basic Info** 页的 `App ID` 和 `App Secret`

## 3. 给应用授权表格

打开 Base,右上角 **… More → Add App**(添加文档应用/Collaborators 中添加应用),搜索刚创建的应用,授予**可阅读(Can view)**权限。

> 找不到入口时,也可以把表格所在知识库/文件夹共享给该应用。

## 4. 获取表格标识

打开 Base,浏览器地址栏形如:

```
https://xxx.larksuite.com/base/AbCdEfGhIjKlMn?table=tblXXXXXXXX&view=vewYYYY
```

- `base/` 后面的一段是 **app_token**(`LARK_BASE_APP_TOKEN`)
- `table=` 参数是 **table_id**(`LARK_TABLE_ID`)

## 5. 填入固件配置

```bash
cp firmware/helper_board/secrets.h.example firmware/helper_board/secrets.h
# 编辑 secrets.h,填 WiFi、App ID/Secret、app_token、table_id、字段名
```

## 6. 先在电脑上验证链路(推荐)

烧录前先用 curl 验证凭据和权限没问题:

```bash
# 1) 换 token
curl -s -X POST https://open.larksuite.com/open-apis/auth/v3/tenant_access_token/internal \
  -H 'Content-Type: application/json' \
  -d '{"app_id":"<APP_ID>","app_secret":"<APP_SECRET>"}'
# 返回 {"code":0,"tenant_access_token":"t-xxx",...}

# 2) 查记录(TOKEN 换成上一步结果)
curl -s -X POST 'https://open.larksuite.com/open-apis/bitable/v1/apps/<APP_TOKEN>/tables/<TABLE_ID>/records/search?page_size=10' \
  -H 'Content-Type: application/json' -H 'Authorization: Bearer <TOKEN>' \
  -d '{"field_names":["日期","早餐","午餐","晚餐","备注"]}'
# 返回 code:0 且 items 里能看到你填的行,即配置成功
```

常见报错:
- `code 99991663/99991661`:token 无效或过期,重新获取
- `code 91402 NOTEXIST`:app_token/table_id 写错,或**没有把表格授权给应用**(第 3 步)
- `code 99991672`:应用没有开通 bitable 权限或版本未发布(第 2 步)
