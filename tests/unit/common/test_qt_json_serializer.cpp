#include "common/qt_json_serializer.hpp"

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

struct QtJsonChild {
  QString label;
  std::int64_t count = 0;

  // clang-format off
  struct Neko {
    static constexpr auto value = NEKO_NAMESPACE::Object(
        "label", &QtJsonChild::label,
        "count", &QtJsonChild::count
    );
  };
  // clang-format on
};

struct QtJsonProbe {
  QString title;
  std::vector<QtJsonChild> children;
  std::optional<QString> note;

  // clang-format off
  struct Neko {
    static constexpr auto value = NEKO_NAMESPACE::Object(
        "title",    &QtJsonProbe::title,
        "children", &QtJsonProbe::children,
        "note",     &QtJsonProbe::note
    );
  };
  // clang-format on
};

struct NativeQtJsonProbe {
  QJsonObject object;
  QJsonArray array;

  // clang-format off
  struct Neko {
    static constexpr auto value = NEKO_NAMESPACE::Object(
        "object", &NativeQtJsonProbe::object,
        "array",  &NativeQtJsonProbe::array
    );
  };
  // clang-format on
};

auto sampleProbe() -> QtJsonProbe {
  return {
      .title = QStringLiteral("原生 QString"),
      .children = {{.label = QStringLiteral("第一项"), .count = 42}},
      .note = QStringLiteral("备注"),
  };
}

template <typename Input>
void expectProbeInput(const Input &input) {
  QtJsonProbe decoded;
  NEKO_NAMESPACE::QtJsonInputSerializer serializer(input);
  ASSERT_TRUE(serializer(decoded))
      << (serializer.error() == nullptr ? ""
                                        : serializer.error()->msg);
  EXPECT_EQ(decoded.title, QStringLiteral("原生 QString"));
  ASSERT_EQ(decoded.children.size(), 1U);
  EXPECT_EQ(decoded.children.front().label, QStringLiteral("第一项"));
  EXPECT_EQ(decoded.children.front().count, 42);
  ASSERT_TRUE(decoded.note);
  EXPECT_EQ(*decoded.note, QStringLiteral("备注"));
}

} // namespace

TEST(QtJsonSerializer, WritesQStringAndReadsAllTextInputs) {
  QString json;
  NEKO_NAMESPACE::QtJsonOutputSerializer serializer(json);
  ASSERT_TRUE(serializer(sampleProbe()));
  ASSERT_TRUE(serializer.end());
  EXPECT_TRUE(json.contains(QStringLiteral("原生 QString")));

  expectProbeInput(json);
  const QByteArray bytes = json.toUtf8();
  expectProbeInput(bytes);

  QtJsonProbe fromCString;
  NEKO_NAMESPACE::QtJsonInputSerializer cStringSerializer(bytes.constData());
  ASSERT_TRUE(cStringSerializer(fromCString));
  EXPECT_EQ(fromCString.title, QStringLiteral("原生 QString"));

  QtJsonProbe fromSizedCString;
  NEKO_NAMESPACE::QtJsonInputSerializer sizedSerializer(
      bytes.constData(), static_cast<std::size_t>(bytes.size()));
  ASSERT_TRUE(sizedSerializer(fromSizedCString));
  EXPECT_EQ(fromSizedCString.children.front().count, 42);
}

TEST(QtJsonSerializer, WritesAndReadsQJsonDocument) {
  QJsonDocument document;
  NEKO_NAMESPACE::QtJsonOutputSerializer serializer(document);
  ASSERT_TRUE(serializer(sampleProbe()));
  ASSERT_TRUE(serializer.end());
  ASSERT_TRUE(document.isObject());
  EXPECT_EQ(document.object().value(QStringLiteral("title")).toString(),
            QStringLiteral("原生 QString"));

  expectProbeInput(document);
}

TEST(QtJsonSerializer, SupportsNativeQtJsonValuesAsFields) {
  NativeQtJsonProbe source{
      .object = {{QStringLiteral("enabled"), true}},
      .array = {1, QStringLiteral("two")},
  };
  QJsonDocument document;
  NEKO_NAMESPACE::QtJsonOutputSerializer output(document);
  ASSERT_TRUE(output(source));
  ASSERT_TRUE(output.end());

  NativeQtJsonProbe decoded;
  NEKO_NAMESPACE::QtJsonInputSerializer input(document);
  ASSERT_TRUE(input(decoded));
  EXPECT_TRUE(decoded.object.value(QStringLiteral("enabled")).toBool());
  ASSERT_EQ(decoded.array.size(), 2);
  EXPECT_EQ(decoded.array.at(1).toString(), QStringLiteral("two"));
}

TEST(QtJsonSerializer, QJsonDocumentOutputRejectsScalarRoot) {
  QJsonDocument document;
  NEKO_NAMESPACE::QtJsonOutputSerializer serializer(document);
  ASSERT_TRUE(serializer(QStringLiteral("scalar")));
  EXPECT_FALSE(serializer.end());
  ASSERT_NE(serializer.error(), nullptr);
  EXPECT_NE(serializer.error()->msg.find("object or array"), std::string::npos);
}

TEST(QtJsonSerializer, PreservesSigned64BitIntegerLimits) {
  for (const std::int64_t expected : {
           std::numeric_limits<std::int64_t>::min(),
           std::numeric_limits<std::int64_t>::max(),
       }) {
    QString json;
    NEKO_NAMESPACE::QtJsonOutputSerializer output(json);
    ASSERT_TRUE(output(expected));
    ASSERT_TRUE(output.end());

    std::int64_t decoded = 0;
    NEKO_NAMESPACE::QtJsonInputSerializer input(json);
    ASSERT_TRUE(input(decoded))
        << (input.error() == nullptr ? "" : input.error()->msg);
    EXPECT_EQ(decoded, expected);
  }
}

TEST(QtJsonSerializer, SupportsScalarRootsWithoutQt69Apis) {
  QString encoded;
  NEKO_NAMESPACE::QtJsonOutputSerializer output(encoded);
  ASSERT_TRUE(output(QStringLiteral("scalar")));
  ASSERT_TRUE(output.end());
  EXPECT_EQ(encoded, QStringLiteral("\"scalar\""));

  QString decoded;
  NEKO_NAMESPACE::QtJsonInputSerializer input(encoded);
  ASSERT_TRUE(input(decoded));
  EXPECT_EQ(decoded, QStringLiteral("scalar"));
}

TEST(QtJsonSerializer, RejectsMultipleRootValues) {
  std::int64_t decoded = 0;
  NEKO_NAMESPACE::QtJsonInputSerializer input(QByteArrayLiteral("1, 2"));
  EXPECT_FALSE(input(decoded));
  ASSERT_NE(input.error(), nullptr);
  EXPECT_NE(input.error()->msg.find("exactly one root"), std::string::npos);
}

TEST(QtJsonSerializer, RejectsUnsignedIntegerAboveQint64) {
  QString json;
  NEKO_NAMESPACE::QtJsonOutputSerializer serializer(json);
  EXPECT_FALSE(serializer(std::numeric_limits<std::uint64_t>::max()));
  ASSERT_NE(serializer.error(), nullptr);
  EXPECT_NE(serializer.error()->msg.find("INT64_MAX"), std::string::npos);
}

#include "common_main.hpp.in"
