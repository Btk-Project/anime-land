# C++ 代码规范

## 可读性和精简

- 代码首先是给人读的。流程应当清楚、短、直观；不要用额外层次、重复检查和辅助函数制造“严谨”的假象。
- 不要为了控制行宽频繁换行。普通屏幕能够完整显示的声明、调用和表达式保持在一行；换行应当表达步骤、分支或并列数据等结构，而不是在每一层调用或每一个参数处机械断开。
- 一个参数包含过长的嵌套表达式时，优先提取具有含义的局部变量，让调用本身保持完整。不要为了少写一个变量把所有转换硬塞进参数，也不要用条件运算符压缩本来更适合分支或局部变量表达的逻辑。
- 抽象必须封装真实行为、策略或差异。只改一个名字的一行转发函数通常应当删除，直接调用原操作。
- 简单类型转换直接写在字段映射处。不要为 `QString`、`std::string`、日期或 optional 的机械转换分别创建一组包装函数。
- 只验证已知会让当前操作无法继续的条件。不要因为未知枚举、尚未认识的服务端数值或本地偏好范围而让整个响应和流程失败。
- 外部结构和字段类型在解析边界检查一次；成功构造后信任对象。不要在每条调用链反复 `ensure`、`valid` 或检查同一状态。
- 如果初始化可能失败，提供明确的初始化函数。确实需要 lazy 初始化时，提供明确的获取函数，并只在该函数内完成首次加载。
- 不为尚不存在的需求增加烟雾式兼容层、兜底分支或“以后可能有用”的封装。

例如，不要把返回语句排成嵌套调用的阶梯：

```cpp
auto msg = QStringLiteral("未知的 TokenStore：%1").arg(QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())));
return ilias::Err(bangumiError(BangumiErrorCode::InvalidConfiguration, msg));
```

## 协程和异步任务

- 返回协程任务的函数不必因此成为协程函数。只转发另一个任务、没有额外语义的函数直接 `return operation();`，不要写 `co_return co_await operation();`。
- 只有确实需要协程帧时才使用 `co_await`：例如局部临时对象必须存活到异步操作结束、等待结果后还要继续处理、需要在本层转换错误或保证清理与执行顺序，或底层只有 awaitable 而本层接口必须物化为任务。
- 审查转发时必须考虑生命周期。被转发任务若按引用使用本层参数、捕获对象或本层构造的临时值，直接返回可能使这些值过早销毁；这种情况保留协程，或先把所有权明确移入返回任务。
- 不要为了表面统一给所有异步接口增加协程帧；接口返回相同的任务类型已经足够统一。

## 命名和上下文

- 名字应当是当前上下文中最短且无歧义的表达，不是把实现说明写成一句函数名。能写 `md5` 时，不写 `functionGeneratesTheCustomLowerCaseMd5Encoding`。
- 目录、文件、命名空间和类已经提供的语境不要在符号名中重复。例如 `namespace sql { execute(); }`，不要写成 `sql_do_execute()`。
- 同一规则适用于文件名。在 `persistence/` 中使用 `backend.cpp`，不要在没有歧义时重复命名为 `database_backend.cpp`。
- 命名空间用于隔离冲突，不用于反复强调符号来源。没有冲突和可读性问题时，可以在项目自己的窄命名空间中引入常用类型，避免满屏限定名。
- `toXxx`/`fromXxx` 必须指向明确的类型、对象或数据表示。`toJson(XXXRequest)` 的目标清楚；`toQt`、`toDatabase`、`convertValue` 的目标不清楚，不应使用。
- 对象映射函数以目标对象命名，例如 `toSummary(SubjectRecord)`；纯机械转换不需要额外命名。

## 数据库和 ORM

- `Form` 是 ORM 层的一等公民，是数据库表在 C++ 中的长期抽象，不是一次性查询构造器。
- Store 的异步 `open()` 直接在长生命周期 `SqlDatabase` 上调用 `create_if_not_exists()`，接收返回的 Form 并一次性构造状态。不得随后再次 `attach()`，也不得用 `SELECT ... LIMIT 0` 重复探测。
- Form 的兼容检查只要求映射列存在且数据库类型可读取/写入。数据库可以包含其他表、额外列、索引、唯一约束、主键、默认值和 CHECK；这些物理细节不必与 C++ 元数据逐项相等。
- 显式 `attach()` 遇到缺少映射列或类型不兼容时返回错误。Store 使用 `create_if_not_exists()` 时不得为了启动审查再追加 attach；既有表问题由实际读写暴露。
- 默认 attach 不推测未来 SQL 会写入什么值，也不比较两侧业务值域。严格模式只有在能定义并静态证明某类约束会让所有相关读写固定冲突时才有意义；没有具体分类前不预设级别。
- 事务中的 Form 从长期 Form 的表名 `bind()` 到事务；`bind()` 不做重复 Schema I/O。
- SQLite、MySQL 等后端差异由 ilias-sql 驱动和方言实现。应用业务层不得查询系统表、执行 PRAGMA 或携带 `foreign_keys`、`journal_mode` 等后端专属策略。

## 本地数据安全

- 不得删除、覆盖、重建或清洗调用方已有的表和数据。
- `create_if_not_exists()` 只补建应用实际需要但不存在的关系；其他对象完全不属于应用的审查范围。
- 当前没有真实的跨版本转换时，不得虚构 `schema_migrations`、版本验证或启动迁移。将来确有不兼容转换时，再提供明确、可恢复的迁移。
- 应用不偷偷修改数据库运行策略。确需改变后端默认行为时使用明确的连接 option；journal/WAL 等策略由部署者或显式配置负责。
- 应用测试覆盖额外表、额外列和额外约束仍可打开；缺少映射列和类型不兼容由 ilias-sql 的显式 attach 测试覆盖。

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
