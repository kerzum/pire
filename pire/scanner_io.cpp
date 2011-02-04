/*
 * scanner_io.cpp -- scanner serialization and deserialization
 *
 * Copyright (c) 2007-2010, Dmitry Prokoptsev <dprokoptsev@gmail.com>,
 *                          Alexander Gololobov <agololobov@gmail.com>
 *
 * This file is part of Pire, the Perl Incompatible
 * Regular Expressions library.
 *
 * Pire is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Pire is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser Public License for more details.
 * You should have received a copy of the GNU Lesser Public License
 * along with Pire.  If not, see <http://www.gnu.org/licenses>.
 */


#include "stub/stl.h"
#include "stub/saveload.h"
#include "scanners/multi.h"
#include "scanners/slow.h"
#include "scanners/simple.h"
#include "scanners/loaded.h"
#include "align.h"
#include "scanners/loaded.h"

namespace Pire {
namespace Impl {
	
	template<>
	void Scanner<Relocatable>::Save(yostream* s) const
	{
		Locals mc = m;
		mc.initial -= reinterpret_cast<size_t>(m_transitions);
		SavePodType(s, Pire::Header(1, sizeof(mc)));
		Impl::AlignSave(s, sizeof(Pire::Header));
		SavePodType(s, mc);
		Impl::AlignSave(s, sizeof(mc));
		SavePodType(s, Settings());
		Impl::AlignSave(s, sizeof(Settings));
		SavePodType(s, Empty());
		Impl::AlignSave(s, sizeof(Empty()));
		if (!Empty())
			Impl::AlignedSaveArray(s, m_buffer, BufSize());
	}

	template<>
	void Scanner<Relocatable>::Load(yistream* s)
	{
		Scanner<Relocatable> sc;
		Impl::ValidateHeader(s, 1, sizeof(sc.m));
		LoadPodType(s, sc.m);
		Impl::AlignLoad(s, sizeof(sc.m));
		Settings actual, required;
		LoadPodType(s, actual);
		Impl::AlignLoad(s, sizeof(actual));
		if (actual != required)
			throw std::runtime_error("This scanner was compiled for an incompatible platform");
		bool empty;
		LoadPodType(s, empty);
		Impl::AlignLoad(s, sizeof(empty));
		
		if (empty) {
			sc.Alias(m_null);
		} else {
			sc.m_buffer = new char[sc.BufSize()];
			Impl::AlignedLoadArray(s, sc.m_buffer, sc.BufSize());
			sc.Markup(sc.m_buffer);
			sc.m.initial += reinterpret_cast<size_t>(sc.m_transitions);
		}
		Swap(sc);
	}
	
	// TODO: implement more effective serialization
	// of nonrelocatable scanner if necessary
	
	template<>
	void Scanner<Nonrelocatable>::Save(yostream* s) const
	{
		Scanner<Relocatable>(*this).Save(s);
	}
	
	template<>
	void Scanner<Nonrelocatable>::Load(yistream* s)
	{
		Scanner<Relocatable> rs;
		rs.Load(s);
		Scanner<Nonrelocatable>(rs).Swap(*this);
	}
}
	
void SimpleScanner::Save(yostream* s) const
{
	YASSERT(m_buffer);
	SavePodType(s, Header(2, sizeof(m)));
	Impl::AlignSave(s, sizeof(Header));
	Locals mc = m;
	mc.initial -= reinterpret_cast<size_t>(m_transitions);
	SavePodType(s, mc);
	Impl::AlignSave(s, sizeof(mc));
	Impl::AlignedSaveArray(s, m_buffer, BufSize());
}

void SimpleScanner::Load(yistream* s)
{
	SimpleScanner sc;
	Impl::ValidateHeader(s, 2, sizeof(sc.m));
	LoadPodType(s, sc.m);
	Impl::AlignLoad(s, sizeof(sc.m));
	sc.m_buffer = new char[sc.BufSize()];
	Impl::AlignedLoadArray(s, sc.m_buffer, sc.BufSize());
	sc.Markup(sc.m_buffer);
	sc.m.initial += reinterpret_cast<size_t>(sc.m_transitions);
	Swap(sc);
}

void SlowScanner::Save(yostream* s) const
{
	YASSERT(!m_vec.empty());
	SavePodType(s, Header(3, sizeof(m)));
	Impl::AlignSave(s, sizeof(Header));
	SavePodType(s, m);
	Impl::AlignSave(s, sizeof(m));
	Impl::AlignedSaveArray(s, m_letters, MaxChar);
	Impl::AlignedSaveArray(s, m_finals, m.statesCount);

	size_t c = 0;
	SavePodType<size_t>(s, 0);
	for (yvector< yvector< unsigned > >::const_iterator i = m_vec.begin(), ie = m_vec.end(); i != ie; ++i) {
		size_t n = c + i->size();
		SavePodType(s, n);
		c = n;
	}
	Impl::AlignSave(s, (m_vec.size() + 1) * sizeof(size_t));

	size_t size = 0;
	for (yvector< yvector< unsigned > >::const_iterator i = m_vec.begin(), ie = m_vec.end(); i != ie; ++i)
		if (!i->empty()) {
			SaveArray(s, &(*i)[0], i->size());
			size += sizeof(unsigned) * i->size();
		}
	Impl::AlignSave(s, size);
}

void SlowScanner::Load(yistream* s)
{
	SlowScanner sc;
	Impl::ValidateHeader(s, 3, sizeof(sc.m));
	LoadPodType(s, sc.m);
	Impl::AlignLoad(s, sizeof(sc.m));
	sc.m_vec.resize(sc.m.lettersCount * sc.m.statesCount);
	sc.m_vecptr = &sc.m_vec;

	sc.alloc(sc.m_letters, MaxChar);
	Impl::AlignedLoadArray(s, sc.m_letters, MaxChar);

	sc.alloc(sc.m_finals, sc.m.statesCount);
	Impl::AlignedLoadArray(s, sc.m_finals, sc.m.statesCount);

	size_t c;
	LoadPodType(s, c);
	for (yvector< yvector< unsigned > >::iterator i = sc.m_vec.begin(), ie = sc.m_vec.end(); i != ie; ++i) {
		size_t n;
		LoadPodType(s, n);
		i->resize(n - c);
		c = n;
	}
	Impl::AlignLoad(s, (m_vec.size() + 1) * sizeof(size_t));

	size_t size = 0;
	for (yvector< yvector< unsigned > >::iterator i = sc.m_vec.begin(), ie = sc.m_vec.end(); i != ie; ++i)
		if (!i->empty()) { 
			LoadArray(s, &(*i)[0], i->size());
			size += sizeof(unsigned) * i->size();
		}
	Impl::AlignLoad(s, size);
	
	Swap(sc);
}

void LoadedScanner::Save(yostream* s) const
{
	SavePodType(s, Header(4, sizeof(m)));
	Impl::AlignSave(s, sizeof(Header));
	Locals mc = m;
	mc.initial -= reinterpret_cast<size_t>(m_jumps);
	SavePodType(s, mc);
	Impl::AlignSave(s, sizeof(mc));

	Impl::AlignedSaveArray(s, m_letters, MaxChar);	
	Impl::AlignedSaveArray(s, m_jumps, m.statesCount * m.lettersCount);	
	Impl::AlignedSaveArray(s, m_actions, m.statesCount * m.lettersCount);
	Impl::AlignedSaveArray(s, m_tags, m.statesCount);
}

void LoadedScanner::Load(yistream* s)
{
	LoadedScanner sc;
	Impl::ValidateHeader(s, 4, sizeof(sc.m));
	LoadPodType(s, sc.m);
	Impl::AlignLoad(s, sizeof(sc.m));
	sc.m_buffer = malloc(sc.BufSize());
	sc.Markup(sc.m_buffer);
	Impl::AlignedLoadArray(s, sc.m_letters, MaxChar);
	Impl::AlignedLoadArray(s, sc.m_jumps, sc.m.statesCount * sc.m.lettersCount);
	Impl::AlignedLoadArray(s, sc.m_actions, sc.m.statesCount * sc.m.lettersCount);
	Impl::AlignedLoadArray(s, sc.m_tags, sc.m.statesCount);
	sc.m.initial += reinterpret_cast<size_t>(sc.m_jumps);
	Swap(sc);
}

}
