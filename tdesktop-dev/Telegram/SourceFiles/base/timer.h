/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QObject>
#include <QtCore/QThread>
#include "base/observer.h"
#include "base/flat_map.h"

namespace base {

class Timer final : private QObject {
public:
	explicit Timer(
		not_null<QThread*> thread,
		Fn<void()> callback = nullptr);
	explicit Timer(Fn<void()> callback = nullptr);

	static Qt::TimerType DefaultType(TimeMs timeout) {
		constexpr auto kThreshold = TimeMs(1000);
		return (timeout > kThreshold) ? Qt::CoarseTimer : Qt::PreciseTimer;
	}

	void setCallback(Fn<void()> callback) {
		_callback = std::move(callback);
	}

	void callOnce(TimeMs timeout) {
		callOnce(timeout, DefaultType(timeout));
	}

	void callEach(TimeMs timeout) {
		callEach(timeout, DefaultType(timeout));
	}

	void callOnce(TimeMs timeout, Qt::TimerType type) {
		start(timeout, type, Repeat::SingleShot);
	}

	void callEach(TimeMs timeout, Qt::TimerType type) {
		start(timeout, type, Repeat::Interval);
	}

	bool isActive() const {
		return (_timerId != 0);
	}

	void cancel();
	TimeMs remainingTime() const;

	static void Adjust();

protected:
	void timerEvent(QTimerEvent *e) override;

private:
	enum class Repeat : unsigned {
		Interval   = 0,
		SingleShot = 1,
	};
	void start(TimeMs timeout, Qt::TimerType type, Repeat repeat);
	void adjust();

	void setTimeout(TimeMs timeout);
	int timeout() const;

	void setRepeat(Repeat repeat) {
		_repeat = static_cast<unsigned>(repeat);
	}
	Repeat repeat() const {
		return static_cast<Repeat>(_repeat);
	}

	Fn<void()> _callback;
	TimeMs _next = 0;
	int _timeout = 0;
	int _timerId = 0;

	Qt::TimerType _type : 2;
	bool _adjusted : 1;
	unsigned _repeat : 1;

};

class DelayedCallTimer final : private QObject {
public:
	int call(TimeMs timeout, FnMut<void()> callback) {
		return call(
			timeout,
			std::move(callback),
			Timer::DefaultType(timeout));
	}

	int call(
		TimeMs timeout,
		FnMut<void()> callback,
		Qt::TimerType type);
	void cancel(int callId);

protected:
	void timerEvent(QTimerEvent *e) override;

private:
	base::flat_map<int, FnMut<void()>> _callbacks;

};

} // namespace base
