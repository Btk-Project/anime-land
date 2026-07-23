# C++ 代码规范

## Neko 反射元数据

- 小型对象在不需要 tag、字段重命名或显式字段列表时，可以省略 `Neko` 定义。
- 一旦定义 `Neko`，`Object(...)` 中每个字段必须独占一行；字段名和值分列，并以当前对象中最长的字段名为基准手动对齐。
- 手动对齐的 `Neko` 块必须使用 `// clang-format off` 和 `// clang-format on` 保护。不要添加不能保护任何手动布局的空格式化开关。
- `make_tags<...>` 只有一个或两个 tag 时保持在同一行，不拆分模板参数。
- `make_tags<...>` 有三个或更多 tag 时，每个 tag 独占一行并纵向对齐。

单个 tag 的示例：

```cpp
// clang-format off
struct Neko {
  static constexpr auto value = Object(
      "name",       &Example::name,
      "totalCount", make_tags<rename_tag<"total_cont">>(&Example::totalCount)
  );
};
// clang-format on
```

多个 tag 的示例：

```cpp
// clang-format off
struct Neko {
  static constexpr auto value = Object(
      "output", 
      make_tags<arg_long_name<"output">,
                arg_value_name<"PATH">,
                arg_help<"output file path">>(&Example::output)
  );
};
// clang-format on
```
