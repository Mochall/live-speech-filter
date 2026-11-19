#include "bz2-unpack.hpp"

#include <QByteArray>
#include <QFile>

#include <bzlib.h>

#include <cstdlib>

// Required by the library when built with BZ_NO_STDIO; called only on an internal inconsistency.
extern "C" void bz_internal_error(int) { std::abort(); }

namespace {

bool Run(QFile &src, QFile &dst, const std::atomic<bool> &cancel, QString &error) {
    QByteArray in(1 << 20, Qt::Uninitialized), out(4 << 20, Qt::Uninitialized);
    bz_stream s{};
    if (BZ2_bzDecompressInit(&s, 0, 0) != BZ_OK) {
        error = "bzip2 初始化失败";
        return false;
    }
    bool ok = false, any = false;
    for (;;) {
        if (cancel.load()) {
            error = "已取消";
            break;
        }
        if (s.avail_in == 0) {
            const qint64 n = src.read(in.data(), in.size());
            if (n < 0) {
                error = "读文件失败";
                break;
            }
            if (n == 0) {
                if (any) ok = true; // ended exactly at a stream end
                else error = "文件不完整";
                break;
            }
            s.next_in = in.data();
            s.avail_in = (unsigned)n;
        }
        s.next_out = out.data();
        s.avail_out = (unsigned)out.size();
        const int r = BZ2_bzDecompress(&s);
        const qint64 produced = out.size() - s.avail_out;
        if (produced > 0 && dst.write(out.constData(), produced) != produced) {
            error = "写文件失败（磁盘满了？）";
            break;
        }
        if (r == BZ_STREAM_END) {
            // Another stream may follow: start again with the bytes that are left.
            char *next = s.next_in;
            const unsigned left = s.avail_in;
            BZ2_bzDecompressEnd(&s);
            s = bz_stream{};
            if (BZ2_bzDecompressInit(&s, 0, 0) != BZ_OK) {
                error = "bzip2 初始化失败";
                return false;
            }
            s.next_in = next;
            s.avail_in = left;
            any = true;
            continue;
        }
        if (r == BZ_DATA_ERROR_MAGIC && any) { // trailing bytes after the last stream (bzip2 ignores them too)
            ok = true;
            break;
        }
        if (r != BZ_OK) {
            error = QString("文件损坏（bzip2 错误 %1）").arg(r);
            break;
        }
        any = false; // inside a stream: the file must not end here
    }
    BZ2_bzDecompressEnd(&s);
    return ok;
}

} // namespace

bool Bunzip2File(const QString &in, const QString &out, const std::atomic<bool> &cancel, QString &error) {
    QFile src(in), dst(out);
    if (!src.open(QIODevice::ReadOnly)) {
        error = "打不开下载的文件";
        return false;
    }
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = "不能在模型文件夹里写文件";
        return false;
    }
    const bool ok = Run(src, dst, cancel, error);
    dst.close();
    if (!ok) QFile::remove(out);
    return ok;
}
