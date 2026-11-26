// This file is part of mokaGram, https://github.com/mokagram/mokagram
// (c) 2026 mokaGram contributors. GPLv3 with the attribution term
// of MOKAGRAM_NOTICE.md (section 7(b)): keep this notice when copying.
#include "mokagram/music/mokagram_youtube_music.h"

#include "logs.h"
#include "mokagram/lang/mokagram_lang.h"
#include "mokagram/music/mokagram_soundcloud.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtCore/QStandardPaths>
#include <QtCore/QTemporaryDir>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>

#include <algorithm>
#include <cmath>
#include <limits>
#include <deque>
#include <map>

namespace Mokagram::Music {
namespace {

constexpr auto kOrigin = "https://music.youtube.com";
constexpr auto kSongFilter = "Eg-KAQwIARAAGAAgACgAMABqChAEEAMQCRAFEAo%3D";
constexpr auto kResponseLimit = 4 * 1024 * 1024;
constexpr auto kAudioLimit = 100 * 1024 * 1024;
constexpr auto kAudioExtensions = { "m4a", "webm" };

struct CatalogConfig {
	QString key;
	QJsonObject client;
	qint64 expires = 0;
};

CatalogConfig &Config() {
	static auto config = CatalogConfig();
	return config;
}

void CatalogRequest(
		const QUrl &url,
		QJsonObject body,
		Fn<void(QByteArray)> done,
		Fn<void(QString)> failed = nullptr) {
	static const auto net = new QNetworkAccessManager(QCoreApplication::instance());
	auto request = QNetworkRequest(url);
	request.setRawHeader("User-Agent", "Mozilla/5.0 (X11; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0");
	request.setRawHeader("Accept-Language", "en-US,en;q=0.9");
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
	request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
	request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
	request.setTransferTimeout(10000);
	const auto bootstrap = body.isEmpty();
	if (!bootstrap) {
		request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
		request.setRawHeader("Origin", kOrigin);
	}
	const auto reply = bootstrap
		? net->get(request)
		: net->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
	QObject::connect(reply, &QNetworkReply::redirected, reply, [=](const QUrl &target) {
		const auto host = target.host();
		if (target.scheme() == u"https"_q && target.port(443) == 443 && target.userInfo().isEmpty()
			&& (host == u"music.youtube.com"_q || (bootstrap && host == u"consent.youtube.com"_q))) {
			reply->redirectAllowed();
		} else {
			reply->abort();
		}
	});
	reply->setReadBufferSize(kResponseLimit + 1);
	QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [=](qint64 received, qint64 total) {
		if (received > kResponseLimit || total > kResponseLimit) reply->abort();
	});
	QObject::connect(reply, &QNetworkReply::finished, reply, [=] {
		const auto error = reply->error();
		const auto message = reply->errorString();
		const auto data = error == QNetworkReply::NoError ? reply->readAll() : QByteArray();
		reply->deleteLater();
		if (failed && (error != QNetworkReply::NoError || data.size() > kResponseLimit)) {
			failed(error != QNetworkReply::NoError
				? u"YouTube Music: "_q + message
				: u"YouTube Music response exceeded the size limit."_q);
		} else {
			done(data.size() <= kResponseLimit ? data : QByteArray());
		}
	});
}

void LoadCatalogConfig(
		Fn<void(CatalogConfig)> done,
		Fn<void(QString)> failed = nullptr) {
	const auto now = QDateTime::currentSecsSinceEpoch();
	if (Config().expires > now) {
		done(Config());
		return;
	}
	CatalogRequest(QUrl(QString::fromLatin1(kOrigin) + u"/?hl=en"_q), {}, [=](QByteArray page) {
		const auto html = QString::fromUtf8(page);
		const auto field = [&](const QString &name) {
			return QRegularExpression(u"\"%1\"\\s*:\\s*\"([^\"]+)\""_q.arg(name))
				.match(html).captured(1);
		};
		const auto key = field(u"INNERTUBE_API_KEY"_q);
		const auto name = field(u"INNERTUBE_CLIENT_NAME"_q);
		const auto version = field(u"INNERTUBE_CLIENT_VERSION"_q);
		if (key.isEmpty() || name.isEmpty() || version.isEmpty()) {
			if (failed) {
				failed(u"Could not parse YouTube Music client configuration."_q);
			} else {
				done({});
			}
			return;
		}
		Config() = {
			.key = key,
			.client = QJsonObject{
				{ "clientName", name },
				{ "clientVersion", version },
				{ "visitorData", field(u"VISITOR_DATA"_q) },
				{ "hl", "en" },
			},
			.expires = now + 6 * 60 * 60,
		};
		done(Config());
	}, failed);
}

[[nodiscard]] QString MatchText(QString text) {
	static const auto separators = QRegularExpression(u"[^\\p{L}\\p{N}]+"_q);
	return text.normalized(QString::NormalizationForm_KC).toCaseFolded().remove(separators);
}

[[nodiscard]] int ReadSeconds(const QString &value) {
	static const auto duration = QRegularExpression(u"^(?:(\\d{1,3}):)?(\\d{1,3}):([0-5]\\d)$"_q);
	const auto match = duration.match(value);
	return match.hasMatch()
		? match.captured(1).toInt() * 3600 + match.captured(2).toInt() * 60 + match.captured(3).toInt()
		: 0;
}

[[nodiscard]] qint64 ReadPlays(const QString &text) {
	// Каталог запрашивается с hl=en. Значение может быть округлено самим YouTube Music.
	static const auto pattern = QRegularExpression(
		u"^(?:(\\d+|\\d{1,3}(?:,\\d{3})+)"
		u"|(\\d+(?:\\.\\d+)?)([KMB]))\\s+plays?$"_q);
	const auto match = pattern.match(text.trimmed());
	if (!match.hasMatch()) {
		return 0;
	}
	if (match.captured(3).isEmpty()) {
		return match.captured(1).remove(u',').toLongLong();
	}
	const auto suffix = match.captured(3).front();
	const auto multiplier = (suffix == u'B') ? 1e9 : (suffix == u'M') ? 1e6 : 1e3;
	const auto count = std::round(match.captured(2).toDouble() * multiplier);
	return (count < double(std::numeric_limits<qint64>::max())) ? qint64(count) : 0;
}

[[nodiscard]] QString VideoType(const QJsonObject &object) {
	return object.value("navigationEndpoint").toObject()
		.value("watchEndpoint").toObject()
		.value("watchEndpointMusicSupportedConfigs").toObject()
		.value("watchEndpointMusicConfig").toObject()
		.value("musicVideoType").toString();
}

struct Match {
	QString id;
	int delta = 13;
};

void FindSong(const QJsonValue &value, const QString &title, const QString &artist, int seconds, Match &best) {
	if (best.delta == 0) return;
	if (value.isArray()) {
		for (const auto &child : value.toArray()) FindSong(child, title, artist, seconds, best);
		return;
	}
	const auto object = value.toObject();
	const auto row = object.value("musicResponsiveListItemRenderer").toObject();
	if (row.isEmpty()) {
		for (auto i = object.constBegin(); i != object.constEnd(); ++i) {
			FindSong(i.value(), title, artist, seconds, best);
		}
		return;
	}
	static const auto videoId = QRegularExpression(u"^[A-Za-z0-9_-]{11}$"_q);
	const auto id = row.value("playlistItemData").toObject().value("videoId").toString();
	const auto type = VideoType(row);
	if (!videoId.match(id).hasMatch() || (!type.isEmpty() && type != u"MUSIC_VIDEO_TYPE_ATV"_q)) return;
	const auto columns = row.value("flexColumns").toArray();
	if (columns.size() < 2) return;
	const auto runs = [&](int index) {
		return columns.at(index).toObject().value("musicResponsiveListItemFlexColumnRenderer")
			.toObject().value("text").toObject().value("runs").toArray();
	};
	auto name = QString();
	for (const auto &entry : runs(0)) {
		const auto run = entry.toObject();
		const auto type = VideoType(run);
		if (!type.isEmpty() && type != u"MUSIC_VIDEO_TYPE_ATV"_q) return;
		name += run.value("text").toString();
	}
	if (MatchText(name) != title) return;
	auto credits = QStringList();
	auto firstCredit = QString();
	auto duration = 0;
	for (const auto &entry : runs(1)) {
		const auto run = entry.toObject();
		const auto text = run.value("text").toString().trimmed();
		const auto channel = run.value("navigationEndpoint").toObject()
			.value("browseEndpoint").toObject().value("browseId").toString();
		if (channel.startsWith(u"UC"_q)) credits.push_back(text);
		if (const auto length = ReadSeconds(text)) {
			duration = length;
		} else if (firstCredit.isEmpty() && text != u"Song"_q && !MatchText(text).isEmpty()) {
			firstCredit = text;
		}
	}
	if (MatchText(credits.empty() ? firstCredit : credits.join(u", "_q)) != artist) return;
	if (seconds > 0 && duration == 0) return;
	const auto delta = seconds > 0 ? std::abs(duration - seconds) : 0;
	if (delta < best.delta) best = { id, delta };
}

[[nodiscard]] QString RowVideoId(const QJsonObject &row) {
	static const auto valid = QRegularExpression(u"^[A-Za-z0-9_-]{11}$"_q);
	auto id = row.value("playlistItemData").toObject()
		.value("videoId").toString();
	if (!valid.match(id).hasMatch()) {
		id = row.value("navigationEndpoint").toObject()
			.value("watchEndpoint").toObject()
			.value("videoId").toString();
	}
	return valid.match(id).hasMatch() ? id : QString();
}

[[nodiscard]] QString LargestThumbnail(const QJsonArray &list) {
	auto result = QString();
	auto bestWidth = -1;
	for (const auto &value : list) {
		const auto thumb = value.toObject();
		const auto url = thumb.value("url").toString();
		const auto width = thumb.value("width").toInt();
		if (!url.isEmpty() && width >= bestWidth) {
			bestWidth = width;
			result = url;
		}
	}
	return result;
}

[[nodiscard]] Track CatalogTrack(const QString &id, QString thumbnail) {
	if (thumbnail.isEmpty()) {
		thumbnail = u"https://i.ytimg.com/vi/"_q + id + u"/hqdefault.jpg"_q;
	}
	auto track = Track();
	track.id = u"yt:"_q + id;
	track.videoId = id;
	track.artworkUrl = std::move(thumbnail);
	track.permalink = QString::fromLatin1(kOrigin) + u"/watch?v="_q + id;
	track.unavailable = true;
	return track;
}

[[nodiscard]] bool IsArtistBrowseId(const QString &id) {
	static const auto valid = QRegularExpression(u"^UC[A-Za-z0-9_-]{22}$"_q);
	return valid.match(id).hasMatch();
}

[[nodiscard]] bool IsAlbumBrowseId(const QString &id) {
	static const auto valid = QRegularExpression(u"^MPRE[A-Za-z0-9_-]+$"_q);
	return valid.match(id).hasMatch();
}

[[nodiscard]] QJsonObject BrowseEndpoint(const QJsonObject &object) {
	return object.value("navigationEndpoint").toObject().value("browseEndpoint").toObject();
}

[[nodiscard]] QString NativeText(const QJsonObject &object) {
	auto result = object.value("simpleText").toString();
	for (const auto &run : object.value("runs").toArray()) {
		result += run.toObject().value("text").toString();
	}
	return result.trimmed();
}

[[nodiscard]] QJsonArray FlexRuns(const QJsonObject &row, int index) {
	const auto columns = row.value("flexColumns").toArray();
	return columns.at(index).toObject().value("musicResponsiveListItemFlexColumnRenderer")
		.toObject().value("text").toObject().value("runs").toArray();
}

void NativeCredits(const QJsonArray &runs, QString &name, QString &id) {
	auto names = QStringList();
	for (const auto &value : runs) {
		const auto run = value.toObject();
		const auto channel = BrowseEndpoint(run).value("browseId").toString();
		if (!IsArtistBrowseId(channel)) {
			continue;
		}
		const auto text = run.value("text").toString().trimmed();
		if (!text.isEmpty() && !names.contains(text)) {
			names.push_back(text);
		}
		if (id.isEmpty()) {
			id = u"yt:"_q + channel;
		}
	}
	if (!names.empty()) {
		name = names.join(u", "_q);
	}
}

[[nodiscard]] QString NativeThumbnail(const QJsonObject &row) {
	auto thumb = row.value("thumbnail").toObject();
	if (thumb.isEmpty()) thumb = row.value("thumbnailRenderer").toObject();
	if (thumb.contains("musicThumbnailRenderer")) {
		thumb = thumb.value("musicThumbnailRenderer").toObject().value("thumbnail").toObject();
	} else if (thumb.contains("croppedSquareThumbnailRenderer")) {
		thumb = thumb.value("croppedSquareThumbnailRenderer").toObject().value("thumbnail").toObject();
	}
	return LargestThumbnail(thumb.value("thumbnails").toArray());
}

void NativeRequest(
		const QString &method,
		QJsonObject body,
		const QString &continuation,
		Fn<void(QJsonObject, QString)> done) {
	LoadCatalogConfig([=](CatalogConfig config) mutable {
		auto url = QUrl(QString::fromLatin1(kOrigin) + u"/youtubei/v1/"_q + method);
		auto query = QUrlQuery();
		query.addQueryItem(u"alt"_q, u"json"_q);
		query.addQueryItem(u"key"_q, config.key);
		if (!continuation.isEmpty()) {
			query.addQueryItem(u"ctoken"_q, continuation);
			query.addQueryItem(u"continuation"_q, continuation);
			body.insert("continuation", continuation);
		}
		url.setQuery(query);
		body.insert("context", QJsonObject{ { "client", config.client } });
		CatalogRequest(url, std::move(body), [=](QByteArray data) {
			auto error = QJsonParseError();
			const auto document = QJsonDocument::fromJson(data, &error);
			if (error.error != QJsonParseError::NoError || !document.isObject()) {
				done({}, u"Could not parse YouTube Music response: "_q + error.errorString());
				return;
			}
			const auto response = document.object();
			if (response.contains("error")) {
				const auto message = response.value("error").toObject().value("message").toString();
				done({}, u"YouTube Music: "_q + message);
				return;
			}
			done(response, {});
		}, [=](QString error) {
			done({}, std::move(error));
		});
	}, [=](QString error) {
		done({}, std::move(error));
	});
}

enum class NativePageKind {
	Artist,
	Songs,
	Releases,
	Album,
};

struct NativePage {
	NativePageKind kind = NativePageKind::Artist;
	QString browseId;
	QString params;
	QString continuation;
	ReleaseKind releaseKind = ReleaseKind::Unknown;
};

// Only walk content-bearing branches, never menu commands, related-page links,
// radio buttons or reload/sort continuations.
constexpr auto kNativeContentKeys = {
	"contents", "content", "tabs", "tabRenderer",
	"singleColumnBrowseResultsRenderer", "twoColumnBrowseResultsRenderer",
	"sectionListRenderer", "secondaryContents", "continuationContents",
	"sectionListContinuation", "musicShelfContinuation",
	"musicPlaylistShelfContinuation", "gridContinuation",
	"onResponseReceivedActions", "onResponseReceivedEndpoints",
	"appendContinuationItemsAction", "continuationItems",
};

[[nodiscard]] ReleaseKind NativeReleaseKind(const QString &text) {
	const auto value = text.trimmed().toCaseFolded();
	return (value == u"album"_q || value == u"albums"_q)
		? ReleaseKind::Album
		: (value == u"single"_q || value == u"singles"_q)
		? ReleaseKind::Single
		: (value == u"ep"_q || value == u"eps"_q)
		? ReleaseKind::Ep
		: (value == u"compilation"_q || value == u"compilations"_q)
		? ReleaseKind::Compilation
		: ReleaseKind::Unknown;
}

[[nodiscard]] QJsonObject NativeFindRenderer(const QJsonValue &value, const QString &key) {
	if (value.isArray()) {
		for (const auto &child : value.toArray()) {
			if (auto result = NativeFindRenderer(child, key); !result.isEmpty()) {
				return result;
			}
		}
	} else if (value.isObject()) {
		const auto object = value.toObject();
		if (const auto result = object.value(key).toObject(); !result.isEmpty()) {
			return result;
		}
		for (const auto branch : kNativeContentKeys) {
			if (auto result = NativeFindRenderer(object.value(branch), key); !result.isEmpty()) {
				return result;
			}
		}
	}
	return {};
}

[[nodiscard]] ArtistInfo NativeArtistInfo(const QJsonObject &response, const QString &id) {
	const auto header = response.value("header").toObject();
	auto artist = header.value("musicImmersiveHeaderRenderer").toObject();
	if (artist.isEmpty()) artist = header.value("musicVisualHeaderRenderer").toObject();
	auto info = ArtistInfo();
	info.name = NativeText(artist.value("title").toObject());
	if (info.name.isEmpty()) return {};
	info.id = u"yt:"_q + id;
	info.bannerUrl = NativeThumbnail(artist);
	info.avatarUrl = NativeThumbnail(QJsonObject{ { "thumbnail", artist.value("foregroundThumbnail") } });
	if (info.avatarUrl.isEmpty()) info.avatarUrl = info.bannerUrl;
	return info;
}

struct NativeArtistCacheEntry {
	QJsonObject response;
	ArtistInfo info;
	qint64 expires = 0;
	std::vector<Fn<void(QJsonObject, QString)>> pending;
};

auto &NativeArtistCache() {
	static auto cache = std::map<QString, NativeArtistCacheEntry>();
	return cache;
}

void NativeArtistPage(const QString &id, Fn<void(QJsonObject, QString)> done) {
	auto &entry = NativeArtistCache()[id];
	if (entry.expires > QDateTime::currentSecsSinceEpoch()) {
		done(entry.response, {});
		return;
	}
	entry.pending.push_back(std::move(done));
	if (entry.pending.size() != 1) return;
	NativeRequest(u"browse"_q, QJsonObject{ { "browseId", id } }, {},
		[=](QJsonObject response, QString error) {
			auto &entry = NativeArtistCache()[id];
			if (error.isEmpty()) {
				entry.info = NativeArtistInfo(response, id);
				if (entry.info.id.isEmpty()) {
					error = u"Could not parse YouTube Music artist profile."_q;
				} else {
					entry.response = response;
					entry.expires = QDateTime::currentSecsSinceEpoch() + 300;
				}
			}
			auto pending = std::move(entry.pending);
			entry.pending.clear();
			for (const auto &callback : pending) callback(response, error);
		});
}

[[nodiscard]] ArtistInfo NativeSearchArtist(const QJsonValue &value, const QString &name) {
	if (value.isArray()) {
		for (const auto &child : value.toArray()) {
			if (auto info = NativeSearchArtist(child, name); !info.id.isEmpty()) return info;
		}
		return {};
	}
	const auto object = value.toObject();
	const auto row = object.value("musicResponsiveListItemRenderer").toObject();
	if (!row.isEmpty()) {
		const auto endpoint = BrowseEndpoint(row);
		const auto id = endpoint.value("browseId").toString();
		const auto type = endpoint.value("browseEndpointContextSupportedConfigs").toObject()
			.value("browseEndpointContextMusicConfig").toObject().value("pageType").toString();
		const auto title = NativeText(QJsonObject{ { "runs", FlexRuns(row, 0) } });
		if (IsArtistBrowseId(id) && type == u"MUSIC_PAGE_TYPE_ARTIST"_q
			&& MatchText(title) == name) {
			auto info = ArtistInfo();
			info.id = u"yt:"_q + id;
			info.name = title;
			info.avatarUrl = NativeThumbnail(row);
			return info;
		}
		return {};
	}
	for (auto i = object.constBegin(); i != object.constEnd(); ++i) {
		if (auto info = NativeSearchArtist(i.value(), name); !info.id.isEmpty()) return info;
	}
	return {};
}

void NativeResolveArtist(
		const QString &name,
		const QString &continuation,
		std::shared_ptr<QSet<QString>> visited,
		Fn<void(ArtistInfo)> done) {
	NativeRequest(u"search"_q, QJsonObject{
		{ "query", name },
		{ "params", "EgWKAQIgAWoMEA4QChADEAQQCRAF" },
	}, continuation, [=](QJsonObject response, QString error) {
		if (!error.isEmpty()) {
			done({});
			return;
		}
		auto info = NativeSearchArtist(response, MatchText(name));
		if (!info.id.isEmpty()) {
			done(std::move(info));
			return;
		}
		auto shelf = NativeFindRenderer(response, u"musicShelfRenderer"_q);
		if (shelf.isEmpty()) {
			const auto tabs = response.value("contents").toObject().value("tabbedSearchResultsRenderer")
				.toObject().value("tabs").toArray();
			shelf = NativeFindRenderer(tabs, u"musicShelfRenderer"_q);
		}
		if (shelf.isEmpty()) shelf = response.value("continuationContents").toObject().value("musicShelfContinuation").toObject();
		const auto token = shelf.value("continuations").toArray().at(0).toObject()
			.value("nextContinuationData").toObject().value("continuation").toString();
		if (token.isEmpty() || visited->contains(token)) {
			done({});
			return;
		}
		visited->insert(token);
		NativeResolveArtist(name, token, visited, done);
	});
}

class NativeCatalogLoad final : public std::enable_shared_from_this<NativeCatalogLoad> {
public:
	QString artistId;
	QString artistName;
	QString albumArtwork;
	std::vector<Track> tracks;
	std::vector<Album> albums;
	Fn<void(std::vector<Track>, std::vector<Album>, QString)> done;

	void start(NativePage page) {
		_album = (page.kind == NativePageKind::Album);
		enqueue(std::move(page));
		next();
	}

private:
	std::deque<NativePage> _pending;
	QSet<QString> _pages;
	QSet<QString> _tracks;
	QSet<QString> _albums;
	QSet<QString> _requestedTokens;
	bool _album = false;
	QString _error;

	void enqueue(NativePage page) {
		const auto key = QString::number(int(page.kind)) + u':'
			+ page.browseId + u':' + page.params + u':' + page.continuation;
		if (_pages.contains(key)) {
			if (!page.continuation.isEmpty() && _requestedTokens.contains(page.continuation)) {
				_error = u"YouTube Music returned a repeated continuation."_q;
			}
			return;
		}
		_pages.insert(key);
		_pending.push_back(std::move(page));
	}

	void endpoint(const QJsonObject &object, NativePage page) {
		auto endpoint = BrowseEndpoint(object);
		if (endpoint.isEmpty()) {
			endpoint = object.value("browseEndpoint").toObject();
		}
		if (endpoint.isEmpty()) {
			return;
		}
		const auto id = endpoint.value("browseId").toString();
		const auto params = endpoint.value("params").toString();
		const auto sectionArtist = id.startsWith(u"MPAD"_q) ? id.mid(4) : id;
		static const auto playlist = QRegularExpression(u"^VL[A-Za-z0-9_-]+$"_q);
		if ((page.kind == NativePageKind::Songs && !playlist.match(id).hasMatch())
			|| (page.kind == NativePageKind::Releases
				&& (!IsArtistBrowseId(sectionArtist) || params.isEmpty()))) {
			_error = u"YouTube Music returned an invalid catalog browse endpoint."_q;
			return;
		}
		page.browseId = id;
		page.params = params;
		page.continuation.clear();
		enqueue(std::move(page));
	}

	void continuations(const QJsonObject &object, const NativePage &page) {
		for (const auto &value : object.value("continuations").toArray()) {
			const auto token = value.toObject().value("nextContinuationData").toObject()
				.value("continuation").toString();
			if (token.isEmpty()) {
				_error = u"Could not parse YouTube Music continuation."_q;
			} else {
				auto more = page;
				more.continuation = token;
				enqueue(std::move(more));
			}
		}
		const auto item = object.value("continuationItemRenderer").toObject();
		if (item.isEmpty()) {
			return;
		}
		const auto endpoint = item.value("continuationEndpoint").toObject();
		auto command = endpoint.value("continuationCommand").toObject();
		if (command.isEmpty()) {
			for (const auto &value : endpoint.value("commandExecutorCommand").toObject()
					.value("commands").toArray()) {
				const auto candidate = value.toObject().value("continuationCommand").toObject();
				if (candidate.value("request").toString() == u"CONTINUATION_REQUEST_TYPE_BROWSE"_q) {
					command = candidate;
					break;
				}
			}
		}
		const auto token = command.value("token").toString();
		if (token.isEmpty()) {
			_error = u"Could not parse YouTube Music continuation command."_q;
		} else {
			auto more = page;
			more.continuation = token;
			enqueue(std::move(more));
		}
	}

	void song(const QJsonObject &row) {
		auto id = RowVideoId(row);
		if (id.isEmpty()) {
			for (const auto &value : FlexRuns(row, 0)) {
				id = RowVideoId(value.toObject());
				if (!id.isEmpty()) break;
			}
		}
		if (id.isEmpty()) {
			const auto play = row.value("overlay").toObject()
				.value("musicItemThumbnailOverlayRenderer").toObject().value("content").toObject()
				.value("musicPlayButtonRenderer").toObject();
			id = RowVideoId(QJsonObject{ { "navigationEndpoint", play.value("playNavigationEndpoint") } });
		}
		if (id.isEmpty()) {
			_error = u"Could not parse a YouTube Music song identity."_q;
			return;
		}
		if (_tracks.contains(id)) {
			return;
		}
		auto track = CatalogTrack(id, NativeThumbnail(row));
		track.title = NativeText(QJsonObject{ { "runs", FlexRuns(row, 0) } });
		const auto columns = row.value("flexColumns").toArray();
		for (auto i = 1; i < columns.size(); ++i) {
			const auto runs = FlexRuns(row, i);
			NativeCredits(runs, track.artist, track.artistId);
			for (const auto &value : runs) {
				const auto text = value.toObject().value("text").toString();
				if (const auto seconds = ReadSeconds(text)) {
					track.durationMs = seconds * 1000;
				}
				if (const auto plays = ReadPlays(text)) track.playbackCount = plays;
			}
		}
		for (const auto &value : row.value("fixedColumns").toArray()) {
			const auto text = value.toObject().value("musicResponsiveListItemFixedColumnRenderer")
				.toObject().value("text").toObject();
			if (const auto seconds = ReadSeconds(NativeText(text))) track.durationMs = seconds * 1000;
		}
		if (track.title.isEmpty()) {
			_error = u"Could not parse a YouTube Music song title."_q;
			return;
		}
		if (track.artist.isEmpty()) track.artist = artistName;
		if (track.artistId.isEmpty()) track.artistId = artistId;
		track.uploaderName = track.artist;
		if (!albumArtwork.isEmpty()) track.artworkUrl = albumArtwork;
		_tracks.insert(id);
		tracks.push_back(std::move(track));
	}

	void release(const QJsonObject &row, ReleaseKind kind) {
		const auto title = row.value("title").toObject();
		auto id = BrowseEndpoint(row).value("browseId").toString();
		for (const auto &value : title.value("runs").toArray()) {
			const auto candidate = BrowseEndpoint(value.toObject()).value("browseId").toString();
			if (!candidate.isEmpty()) id = candidate;
		}
		if (!IsAlbumBrowseId(id)) {
			_error = u"Could not parse a YouTube Music album browse identity."_q;
			return;
		}
		if (_albums.contains(id)) return;
		auto album = Album();
		album.id = u"yt:"_q + id;
		album.title = NativeText(title);
		album.artist = artistName;
		album.artistId = artistId;
		album.releaseKind = kind;
		album.artworkUrl = NativeThumbnail(row);
		album.backdropArtworkUrl = album.artworkUrl;
		for (const auto &value : row.value("subtitle").toObject().value("runs").toArray()) {
			const auto text = value.toObject().value("text").toString().trimmed();
			if (const auto parsed = NativeReleaseKind(text); parsed != ReleaseKind::Unknown) {
				album.releaseKind = parsed;
			}
			auto ok = false;
			const auto year = text.toInt(&ok);
			if (ok && year >= 1000 && year <= 9999) album.releaseYear = year;
		}
		NativeCredits(row.value("subtitle").toObject().value("runs").toArray(), album.artist, album.artistId);
		if (album.title.isEmpty()) {
			_error = u"Could not parse a YouTube Music release title."_q;
			return;
		}
		_albums.insert(id);
		albums.push_back(std::move(album));
	}

	void parse(const QJsonValue &value, const NativePage &page, bool &recognized) {
		if (value.isArray()) {
			for (const auto &child : value.toArray()) parse(child, page, recognized);
			return;
		}
		if (!value.isObject()) return;
		const auto object = value.toObject();
		continuations(object, page);
		for (const auto key : { "musicShelfRenderer", "musicPlaylistShelfRenderer",
				"musicCarouselShelfRenderer", "gridRenderer" }) {
			const auto shelf = object.value(key).toObject();
			if (shelf.isEmpty()) continue;
			auto section = page;
			if (page.kind == NativePageKind::Artist) {
				const auto carousel = shelf.value("header").toObject()
					.value("musicCarouselShelfBasicHeaderRenderer").toObject();
				auto title = shelf.value("title").toObject();
				if (title.isEmpty()) title = carousel.value("title").toObject();
				const auto name = NativeText(title).toCaseFolded();
				section.releaseKind = NativeReleaseKind(name);
				if (name == u"songs"_q) {
					section.kind = NativePageKind::Songs;
				} else if (section.releaseKind != ReleaseKind::Unknown || name == u"singles & eps"_q) {
					section.kind = NativePageKind::Releases;
				} else {
					continue;
				}
				endpoint(title, section);
				for (const auto &run : title.value("runs").toArray()) endpoint(run.toObject(), section);
				endpoint(shelf.value("bottomEndpoint").toObject(), section);
				endpoint(carousel.value("moreContentButton").toObject().value("buttonRenderer").toObject(), section);
			} else if (page.kind == NativePageKind::Album
				&& (QLatin1StringView(key) == u"musicCarouselShelfRenderer"_q
					|| QLatin1StringView(key) == u"gridRenderer"_q)) {
				continue;
			}
			if (!shelf.value("contents").isArray() && !shelf.value("items").isArray()) {
				_error = u"Could not parse YouTube Music catalog shelf."_q;
			}
			recognized = true;
			parse(shelf, section, recognized);
		}
		if (const auto row = object.value("musicResponsiveListItemRenderer").toObject(); !row.isEmpty()) {
			if (page.kind == NativePageKind::Songs || page.kind == NativePageKind::Album) {
				recognized = true;
				song(row);
			}
		}
		if (const auto row = object.value("musicTwoRowItemRenderer").toObject(); !row.isEmpty()) {
			if (page.kind == NativePageKind::Releases) {
				recognized = true;
				release(row, page.releaseKind);
			}
		}
		for (const auto branch : kNativeContentKeys) {
			if (object.contains(branch)) {
				if (QLatin1StringView(branch).endsWith(QLatin1StringView("Continuation"))
					|| QLatin1StringView(branch) == u"appendContinuationItemsAction"_q) {
					recognized = true;
					const auto content = object.value(branch).toObject();
					if (!content.value("contents").isArray()
						&& !content.value("items").isArray()
						&& !content.value("continuationItems").isArray()) {
						_error = u"Could not parse YouTube Music continuation contents."_q;
					}
				}
				parse(object.value(branch), page, recognized);
			}
		}
		if (object.contains("items")) parse(object.value("items"), page, recognized);
	}

	void next() {
		if (!_error.isEmpty() || _pending.empty()) {
			if (_album && tracks.empty() && _error.isEmpty()) {
				_error = u"YouTube Music album contains no readable tracks."_q;
			}
			done(std::move(tracks), std::move(albums), std::move(_error));
			return;
		}
		auto page = std::move(_pending.front());
		_pending.pop_front();
		if (!page.continuation.isEmpty()) _requestedTokens.insert(page.continuation);
		auto body = QJsonObject{ { "browseId", page.browseId } };
		if (!page.params.isEmpty()) body.insert("params", page.params);
		const auto self = shared_from_this();
		auto completed = Fn<void(QJsonObject, QString)>(
			[self, page](QJsonObject response, QString error) {
				if (!error.isEmpty()) {
					self->_error = std::move(error);
					self->next();
					return;
				}
				auto recognized = false;
				if (page.kind == NativePageKind::Artist && page.continuation.isEmpty()) {
					const auto &info = NativeArtistCache().at(page.browseId).info;
					self->artistName = info.name;
					recognized = !info.id.isEmpty();
				} else if (page.kind == NativePageKind::Album && page.continuation.isEmpty()) {
					auto header = NativeFindRenderer(response.value("contents"), u"musicResponsiveHeaderRenderer"_q);
					if (header.isEmpty()) header = response.value("header").toObject().value("musicDetailHeaderRenderer").toObject();
					if (header.isEmpty()) {
						self->_error = u"Could not parse YouTube Music album header."_q;
					} else {
						NativeCredits(header.value("straplineTextOne").toObject().value("runs").toArray(),
							self->artistName, self->artistId);
						NativeCredits(header.value("subtitle").toObject().value("runs").toArray(),
							self->artistName, self->artistId);
						self->albumArtwork = NativeThumbnail(header);
					}
				}
				self->parse(response, page, recognized);
				if (!recognized && self->_error.isEmpty()) {
					self->_error = u"Could not parse YouTube Music catalog contents."_q;
				}
				self->next();
			});
		if (page.kind == NativePageKind::Artist && page.continuation.isEmpty()) {
			NativeArtistPage(page.browseId, std::move(completed));
		} else {
			NativeRequest(u"browse"_q, std::move(body), page.continuation, std::move(completed));
		}
	}
};

[[nodiscard]] std::vector<Track> ParseRecommendations(const QJsonObject &response) {
	const auto tabs = response.value("contents").toObject()
		.value("singleColumnMusicWatchNextResultsRenderer").toObject()
		.value("tabbedRenderer").toObject()
		.value("watchNextTabbedResultsRenderer").toObject().value("tabs").toArray();
	const auto rows = tabs.at(0).toObject().value("tabRenderer").toObject()
		.value("content").toObject().value("musicQueueRenderer").toObject()
		.value("content").toObject().value("playlistPanelRenderer").toObject()
		.value("contents").toArray();
	auto result = std::vector<Track>();
	static const auto valid = QRegularExpression(u"^[A-Za-z0-9_-]{11}$"_q);
	for (const auto &entry : rows) {
		auto item = entry.toObject();
		if (item.contains("playlistPanelVideoWrapperRenderer")) {
			item = item.value("playlistPanelVideoWrapperRenderer").toObject()
				.value("primaryRenderer").toObject();
		}
		const auto row = item.value("playlistPanelVideoRenderer").toObject();
		const auto id = row.value("videoId").toString();
		if (!valid.match(id).hasMatch() || row.contains("unplayableText")) {
			continue;
		}
		const auto title = row.value("title").toObject()
			.value("runs").toArray().at(0).toObject().value("text").toString().trimmed();
		const auto byline = row.value("longBylineText").toObject().value("runs").toArray();
		auto credits = QStringList();
		for (const auto &entry : byline) {
			const auto run = entry.toObject();
			const auto channel = run.value("navigationEndpoint").toObject()
				.value("browseEndpoint").toObject().value("browseId").toString();
			if (channel.startsWith(u"UC"_q)) {
				credits.push_back(run.value("text").toString());
			}
		}
		const auto artist = credits.empty()
			? byline.at(0).toObject().value("text").toString().trimmed()
			: credits.join(u", "_q);
		if (title.isEmpty() || artist.isEmpty()) {
			continue;
		}
		auto track = CatalogTrack(id, LargestThumbnail(row.value("thumbnail").toObject()
			.value("thumbnails").toArray()));
		track.title = title;
		track.artist = artist;
		NativeCredits(byline, track.artist, track.artistId);
		track.uploaderName = artist;
		track.durationMs = ReadSeconds(row.value("lengthText").toObject()
			.value("runs").toArray().at(0).toObject().value("text").toString()) * 1000;
		result.push_back(std::move(track));
	}
	return result;
}

void SearchSong(const Track &track, Fn<void(QString)> done) {
	LoadCatalogConfig([=](CatalogConfig config) {
		if (config.key.isEmpty()) {
			done({});
			return;
		}
		auto url = QUrl(QString::fromLatin1(kOrigin) + u"/youtubei/v1/search"_q);
		auto query = QUrlQuery();
		query.addQueryItem(u"alt"_q, u"json"_q);
		query.addQueryItem(u"key"_q, config.key);
		url.setQuery(query);
		CatalogRequest(url, QJsonObject{
			{ "context", QJsonObject{ { "client", config.client } } },
			{ "query", (track.artist + u' ' + track.title).simplified() },
			{ "params", QString::fromLatin1(kSongFilter) },
		}, [=](QByteArray data) {
			if (data.isEmpty()) Config() = {};
			const auto contents = QJsonDocument::fromJson(data).object().value("contents");
			auto match = Match();
			FindSong(contents, MatchText(track.title), MatchText(track.artist), track.durationMs / 1000, match);
			done(match.id);
		});
	});
}

[[nodiscard]] QString FindTool(const QString &name) {
	const auto data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
	const auto scoped = QStandardPaths::findExecutable(name, {
		QCoreApplication::applicationDirPath(),
		data + u"/bunnyGram/tools"_q,
	});
	if (!scoped.isEmpty()) return scoped;
	const auto onPath = QStandardPaths::findExecutable(name);
	if (!onPath.isEmpty()) return onPath;
#ifdef Q_OS_MAC
	return QStandardPaths::findExecutable(name, {
		u"/opt/homebrew/bin"_q, u"/usr/local/bin"_q, u"/opt/local/bin"_q,
	});
#else // Q_OS_MAC
	return {};
#endif // Q_OS_MAC
}

[[nodiscard]] QString CachedAudio(const QString &prefix) {
	for (const auto extension : kAudioExtensions) {
		const auto file = QFileInfo(prefix + QString::fromLatin1(extension));
		if (file.isFile() && file.size() > 0 && file.size() <= kAudioLimit) return file.filePath();
	}
	return {};
}

[[nodiscard]] QString DownloadError() {
	return QString::fromUtf8(Lang::Tr("Не удалось воспроизвести трек из YouTube Music"));
}

void DownloadSong(const QString &tool, const QString &id, const QString &cachePrefix, AudioFileDone done) {
	QDir().mkpath(CacheDir());
	const auto temp = std::make_shared<QTemporaryDir>(CacheDir() + u"/yt-XXXXXX"_q);
	if (!temp->isValid()) {
		done({}, DownloadError());
		return;
	}
	const auto process = new QProcess(QCoreApplication::instance());
	process->setStandardOutputFile(QProcess::nullDevice());
	process->setStandardErrorFile(temp->filePath(u"yt-dlp.log"_q));
	QObject::connect(process, &QProcess::errorOccurred, process, [=](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) {
			LOG(("[yt] yt-dlp failed to start: %1").arg(process->errorString()));
			process->deleteLater();
			done({}, DownloadError());
		}
	});
	QObject::connect(process, &QProcess::finished, process, [=](int code, QProcess::ExitStatus status) {
		process->deleteLater();
		const auto file = code == 0 && status == QProcess::NormalExit
			? CachedAudio(temp->filePath(u"audio."_q))
			: QString();
		if (file.isEmpty()) {
			auto log = QFile(temp->filePath(u"yt-dlp.log"_q));
			if (log.open(QIODevice::ReadOnly)) {
				const auto text = QString::fromUtf8(log.readAll()).right(2000);
				LOG(("[yt] download failed %1 code %2 status %3: %4")
					.arg(id)
					.arg(code)
					.arg(status == QProcess::NormalExit ? 1 : 0)
					.arg(text));
			}
			done({}, DownloadError());
			return;
		}
		if (const auto cached = CachedAudio(cachePrefix); !cached.isEmpty()) {
			done(cached, {});
			return;
		}
		const auto path = cachePrefix + QFileInfo(file).suffix();
		const auto existing = QFileInfo(path);
		if (existing.isFile() && (existing.size() == 0 || existing.size() > kAudioLimit)) {
			QFile::remove(path);
		}
		const auto saved = QFile::rename(file, path) ? path : CachedAudio(cachePrefix);
		done(saved, saved.isEmpty() ? DownloadError() : QString());
	});
	QTimer::singleShot(60000, process, [=] { process->kill(); });
	auto args = QStringList{
		u"--ignore-config"_q, u"--no-plugin-dirs"_q, u"--no-playlist"_q,
		u"--no-progress"_q, u"--socket-timeout"_q, u"10"_q, u"--retries"_q, u"1"_q,
		u"--max-filesize"_q, u"100M"_q, u"--check-formats"_q,
		u"--format"_q, u"bestaudio[ext=m4a]/bestaudio[ext=webm]"_q,
		u"--output"_q, temp->filePath(u"audio.%(ext)s"_q),
	};
	for (const auto &runtime : { u"deno"_q, u"node"_q }) {
		const auto path = FindTool(runtime);
		if (!path.isEmpty()) args << u"--js-runtimes"_q << (runtime + u':' + path);
	}
	args << (QString::fromLatin1(kOrigin) + u"/watch?v="_q + id);
	process->start(tool, args, QIODevice::ReadOnly);
}

} // namespace

void ResolveYouTubeMusicArtist(const QString &name, Fn<void(ArtistInfo)> done) {
	const auto query = name.trimmed();
	if (query.isEmpty()) {
		done({});
		return;
	}
	NativeResolveArtist(query, {}, std::make_shared<QSet<QString>>(), std::move(done));
}

void LoadYouTubeMusicArtistInfo(const QString &artistId, Fn<void(ArtistInfo)> done) {
	const auto id = artistId.startsWith(u"yt:"_q) ? artistId.mid(3) : QString();
	if (!IsArtistBrowseId(id)) {
		done({});
		return;
	}
	NativeArtistPage(id, [=](QJsonObject, QString error) {
		done(error.isEmpty() ? NativeArtistCache().at(id).info : ArtistInfo());
	});
}

void LoadYouTubeMusicArtistCatalog(
		const QString &artistId,
		Fn<void(std::vector<Track>, std::vector<Album>, QString)> done) {
	const auto id = artistId.startsWith(u"yt:"_q) ? artistId.mid(3) : QString();
	if (!IsArtistBrowseId(id)) {
		done({}, {}, u"Invalid YouTube Music artist id."_q);
		return;
	}
	auto load = std::make_shared<NativeCatalogLoad>();
	load->artistId = artistId;
	load->done = std::move(done);
	load->start({ .kind = NativePageKind::Artist, .browseId = id });
}

void LoadYouTubeMusicAlbum(
		const QString &albumId,
		Fn<void(std::vector<Track>, QString)> done) {
	const auto id = albumId.startsWith(u"yt:"_q) ? albumId.mid(3) : QString();
	if (!IsAlbumBrowseId(id)) {
		done({}, u"Invalid YouTube Music album id."_q);
		return;
	}
	auto load = std::make_shared<NativeCatalogLoad>();
	load->done = [=](std::vector<Track> tracks, std::vector<Album>, QString error) {
		done(std::move(tracks), std::move(error));
	};
	load->start({ .kind = NativePageKind::Album, .browseId = id });
}

bool HasYouTubeMusicMetadata(const Track &track) {
	return (track.id.toULongLong() || !track.videoId.isEmpty())
		&& !MatchText(track.title).isEmpty()
		&& !MatchText(track.artist).isEmpty();
}

void LoadYouTubeMusicRecommendations(
		const QString &videoId,
		Fn<void(std::vector<Track>)> done) {
	LoadCatalogConfig([=](CatalogConfig config) {
		if (config.key.isEmpty()) {
			done({});
			return;
		}
		auto url = QUrl(QString::fromLatin1(kOrigin) + u"/youtubei/v1/next"_q);
		auto query = QUrlQuery();
		query.addQueryItem(u"alt"_q, u"json"_q);
		query.addQueryItem(u"key"_q, config.key);
		url.setQuery(query);
		CatalogRequest(url, QJsonObject{
			{ "context", QJsonObject{ { "client", config.client } } },
			{ "videoId", videoId },
			{ "playlistId", u"RDAMVM"_q + videoId },
			{ "params", "wAEB" },
			{ "enablePersistentPlaylistPanel", true },
			{ "isAudioOnly", true },
			{ "tunerSettingValue", "AUTOMIX_SETTING_NORMAL" },
		}, [=](QByteArray data) {
			if (data.isEmpty()) Config() = {};
			done(ParseRecommendations(QJsonDocument::fromJson(data).object()));
		});
	});
}

void DownloadYouTubeMusic(const Track &track, AudioFileDone done) {
	if (!HasYouTubeMusicMetadata(track)) {
		done({}, DownloadError());
		return;
	}
	const auto known = !track.videoId.isEmpty();
	const auto key = known
		? track.videoId
		: QString::number(track.id.toULongLong());
	const auto prefix = CacheDir() + (known ? u"/yt-v-"_q : u"/yt-"_q) + key + u'.';
	LoadCachedAudio(prefix, CachedAudio(prefix), [=](AudioFileDone finish) {
		const auto tool = FindTool(u"yt-dlp"_q);
		if (tool.isEmpty()) {
			LOG(("[yt] yt-dlp not found"));
			finish({}, QString::fromUtf8(Lang::Tr("Для воспроизведения из YouTube Music установите yt-dlp")));
			return;
		}
		if (known) {
			DownloadSong(tool, track.videoId, prefix, finish);
			return;
		}
		SearchSong(track, [=](QString videoId) {
			if (videoId.isEmpty()) {
				finish({}, DownloadError());
				return;
			}
			DownloadSong(tool, videoId, prefix, finish);
		});
	}, std::move(done));
}

} // namespace Mokagram::Music
