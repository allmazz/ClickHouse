---
slug: /ja/operations/settings/memory-overcommit
---
# メモリーオーバーコミット

メモリーオーバーコミットは、クエリのより柔軟なメモリー制限を設定するためのエクスペリメンタルな技術です。

この技術のアイデアは、クエリが使用できるメモリーの保証された量を表す設定を導入することです。メモリーオーバーコミットが有効になり、メモリー制限に達した場合、ClickHouseは最もオーバーコミットされたクエリを選択し、このクエリを終了させてメモリーを解放しようとします。

メモリー制限に達した場合、クエリは新しいメモリーを割り当てようとする際に一定時間待機します。タイムアウトが発生し、メモリーが解放されれば、クエリは実行を続行します。それ以外の場合、例外がスローされ、クエリが終了されます。

クエリを停止または終了する選択は、グローバルまたはユーザーのオーバーコミットトラッカーによって行われ、どのメモリー制限に到達したかに依存します。オーバーコミットトラッカーが停止すべきクエリを選べない場合、MEMORY_LIMIT_EXCEEDEDの例外がスローされます。

## ユーザーオーバーコミットトラッカー

ユーザーオーバーコミットトラッカーは、ユーザーのクエリリスト内で最もオーバーコミット率の高いクエリを見つけます。クエリのオーバーコミット率は、割り当てられたバイト数を`memory_overcommit_ratio_denominator`設定値で割ったものとして計算されます。

クエリの`memory_overcommit_ratio_denominator`がゼロの場合、オーバーコミットトラッカーはこのクエリを選択しません。

待機タイムアウトは`memory_usage_overcommit_max_wait_microseconds`設定で設定されます。

**例**

```sql
SELECT number FROM numbers(1000) GROUP BY number SETTINGS memory_overcommit_ratio_denominator=4000, memory_usage_overcommit_max_wait_microseconds=500
```

## グローバルオーバーコミットトラッカー

グローバルオーバーコミットトラッカーは、すべてのクエリのリスト内で最もオーバーコミット率の高いクエリを見つけます。この場合、オーバーコミット率は割り当てられたバイト数を`memory_overcommit_ratio_denominator_for_user`設定値で割ったものとして計算されます。

クエリの`memory_overcommit_ratio_denominator_for_user`がゼロの場合、オーバーコミットトラッカーはこのクエリを選択しません。

待機タイムアウトは、設定ファイル内の`memory_usage_overcommit_max_wait_microseconds`パラメータで設定されます。
