#include "adapters/episode_provider_js/html_bridge.hpp"

#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>

#include <QByteArray>

#include <memory>

namespace anime_land::episode_provider_js {
namespace {

struct HtmlDocumentDeleter {
    void operator()(htmlDocPtr document) const { xmlFreeDoc(document); }
};

struct XPathContextDeleter {
    void operator()(xmlXPathContextPtr context) const {
        xmlXPathFreeContext(context);
    }
};

struct XPathObjectDeleter {
    void operator()(xmlXPathObjectPtr object) const {
        xmlXPathFreeObject(object);
    }
};

auto evaluateString(xmlXPathContextPtr context, const QString &expression)
    -> QString {
    const QByteArray encoded = expression.toUtf8();
    std::unique_ptr<xmlXPathObject, XPathObjectDeleter> value(
        xmlXPathEvalExpression(
            reinterpret_cast<const xmlChar *>(encoded.constData()), context));
    if (!value) {
        return {};
    }
    xmlChar *raw = xmlXPathCastToString(value.get());
    if (raw == nullptr) {
        return {};
    }
    const QString result = QString::fromUtf8(reinterpret_cast<const char *>(raw));
    xmlFree(raw);
    return result;
}

} // namespace

QVariantList HtmlBridge::queryAll(const QString &source, const QString &xpath,
                                  const QVariantMap &fields) const {
    constexpr qsizetype kMaximumDocumentBytes = 8 * 1024 * 1024;
    constexpr qsizetype kMaximumRows = 256;
    constexpr qsizetype kMaximumFields = 32;
    const QByteArray bytes = source.toUtf8();
    if (bytes.isEmpty() || bytes.size() > kMaximumDocumentBytes ||
        xpath.isEmpty() || fields.isEmpty() || fields.size() > kMaximumFields) {
        return {};
    }

    std::unique_ptr<xmlDoc, HtmlDocumentDeleter> document(htmlReadMemory(
        bytes.constData(), static_cast<int>(bytes.size()), nullptr, "UTF-8",
        HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING |
            HTML_PARSE_NONET | HTML_PARSE_COMPACT));
    if (!document) {
        return {};
    }
    std::unique_ptr<xmlXPathContext, XPathContextDeleter> context(
        xmlXPathNewContext(document.get()));
    if (!context) {
        return {};
    }
    const QByteArray encodedXPath = xpath.toUtf8();
    std::unique_ptr<xmlXPathObject, XPathObjectDeleter> selection(
        xmlXPathEvalExpression(
            reinterpret_cast<const xmlChar *>(encodedXPath.constData()),
            context.get()));
    if (!selection || selection->type != XPATH_NODESET ||
        selection->nodesetval == nullptr) {
        return {};
    }

    QVariantList rows;
    const int count = std::min(selection->nodesetval->nodeNr,
                               static_cast<int>(kMaximumRows));
    rows.reserve(count);
    for (int index = 0; index < count; ++index) {
        xmlNodePtr node = selection->nodesetval->nodeTab[index];
        if (node == nullptr) {
            continue;
        }
        context->node = node;
        QVariantMap row;
        for (auto field = fields.constBegin(); field != fields.constEnd(); ++field) {
            if (field.value().metaType().id() != QMetaType::QString) {
                continue;
            }
            row.insert(field.key(),
                       evaluateString(context.get(), field.value().toString()));
        }
        rows.push_back(row);
    }
    return rows;
}

} // namespace anime_land::episode_provider_js
