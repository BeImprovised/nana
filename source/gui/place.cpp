/*
 *	An Implementation of Place for Layout
 *	Nana C++ Library(http://www.nanapro.org)
 *	Copyright(C) 2003-2015 Jinhao(cnjinhao@hotmail.com)
 *
 *	Distributed under the Boost Software License, Version 1.0.
 *	(See accompanying file LICENSE_1_0.txt or copy at
 *	http://www.boost.org/LICENSE_1_0.txt)
 *
 *	@file: nana/gui/place.cpp
 *	@contributors: qPCR4vir
 */

#include <cfloat>
#include <cmath>
#include <map>
#include <deque>
#include <nana/gui/place.hpp>
#include <nana/gui/programming_interface.hpp>
#include <nana/gui/widgets/label.hpp>
#include <nana/gui/widgets/panel.hpp>
#include <nana/gui/dragger.hpp>
#include <nana/gui/drawing.hpp>

#include <memory>
#include <limits>	//numeric_limits

#include "place_parts.hpp"

namespace nana
{
	namespace place_parts
	{
		//check the name
		void check_field_name(const char* name)
		{
			if (*name && (*name != '_' && !(('a' <= *name && *name <= 'z') || ('A' <= *name && *name <= 'Z'))))
				throw std::invalid_argument("nana.place: bad field name");
		}
	}//end namespace place_parts

	typedef place_parts::number_t number_t;
	typedef place_parts::repeated_array repeated_array;

	namespace place_parts
	{
		class tokenizer
		{
		public:
			enum class token
			{
				div_start, div_end, splitter,
				identifier, dock, vert, grid, number, array, reparray,
				weight, gap, margin, arrange, variable, repeated, min_px, max_px,
				collapse, parameters,
				equal,
				eof, error
			};

			tokenizer(const char* p)
				: divstr_(p), sp_(p)
			{}

			const std::string& idstr() const
			{
				return idstr_;
			}

			number_t number() const
			{
				return number_;
			}

			std::vector<number_t>& array()
			{
				return array_;
			}

			repeated_array&& reparray()
			{
				return std::move(reparray_);
			}

			std::vector<number_t>& parameters()
			{
				return parameters_;
			}

			std::size_t pos() const
			{
				return (sp_ - divstr_);
			}

			std::string pos_str() const
			{
				return std::to_string(pos());
			}

			token read()
			{
				sp_ = _m_eat_whitespace(sp_);

				std::size_t readbytes = 0;
				switch (*sp_)
				{
				case '\0':
					return token::eof;
				case '|':
					++sp_;
					readbytes = _m_number(sp_, false);
					sp_ += readbytes;
					return token::splitter;
				case '=':
					++sp_;
					return token::equal;
				case '<':
					++sp_;
					return token::div_start;
				case '>':
					++sp_;
					return token::div_end;
				case '[':
					array_.clear();
					sp_ = _m_eat_whitespace(sp_ + 1);
					if (*sp_ == ']')
					{
						++sp_;
						return token::array;
					}
					else
					{
						//When search the repeated.
						bool repeated = false;

						while (true)
						{
							sp_ = _m_eat_whitespace(sp_);
							auto tk = read();
							if (token::number != tk && token::variable != tk && token::repeated != tk)
								_m_throw_error("invalid array element");

							if (!repeated)
							{
								switch (tk)
								{
								case token::number:
									array_.push_back(number_);
									break;
								case token::variable:
									array_.push_back({});
									break;
								default:
									repeated = true;
									reparray_.repeated();
									reparray_.assign(std::move(array_));
								}
							}

							sp_ = _m_eat_whitespace(sp_);
							char ch = *sp_++;

							if (ch == ']')
								return (repeated ? token::reparray : token::array);

							if (ch != ',')
								_m_throw_error("invalid array");
						}
					}
					break;
				case '(':
					parameters_.clear();
					sp_ = _m_eat_whitespace(sp_ + 1);
					if (*sp_ == ')')
					{
						++sp_;
						return token::parameters;
					}

					while (true)
					{
						if (token::number == read())
							parameters_.push_back(number_);
						else
							_m_throw_error("invalid parameter.");

						sp_ = _m_eat_whitespace(sp_);
						char ch = *sp_++;

						if (ch == ')')
							return token::parameters;

						if (ch != ',')
							_m_throw_error("invalid parameter.");
					}
					break;
				case '.': case '-':
					if (*sp_ == '-')
					{
						readbytes = _m_number(sp_ + 1, true);
						if (readbytes)
							++readbytes;
					}
					else
						readbytes = _m_number(sp_, false);

					if (readbytes)
					{
						sp_ += readbytes;
						return token::number;
					}
					else
						_m_throw_error("invalid character '" + std::string(1, *sp_) + "'");
					break;
				default:
					if ('0' <= *sp_ && *sp_ <= '9')
					{
						readbytes = _m_number(sp_, false);
						if (readbytes)
						{
							sp_ += readbytes;
							return token::number;
						}
					}
					break;
				}

				if ('_' == *sp_ || isalpha(*sp_))
				{
					const char * idstart = sp_++;

					while ('_' == *sp_ || isalpha(*sp_) || isalnum(*sp_))
						++sp_;

					idstr_.assign(idstart, sp_);

					if ("weight" == idstr_ || "min" == idstr_ || "max" == idstr_)
					{
						auto ch = idstr_[1];
						_m_attr_number_value();
						switch (ch)
						{
						case 'e': return token::weight;
						case 'i': return token::min_px;
						case 'a': return token::max_px;
						}
					}
					else if ("dock" == idstr_)
						return token::dock;
					else if ("vertical" == idstr_ || "vert" == idstr_)
						return token::vert;
					else if ("variable" == idstr_ || "repeated" == idstr_)
						return ('v' == idstr_[0] ? token::variable : token::repeated);
					else if ("arrange" == idstr_ || "gap" == idstr_)
					{
						auto ch = idstr_[0];
						_m_attr_reparray();
						return ('a' == ch ? token::arrange : token::gap);
					}
					else if ("grid" == idstr_ || "margin" == idstr_)
					{
						auto idstr = idstr_;
						if (token::equal != read())
							_m_throw_error("an equal sign is required after '" + idstr + "'");

						return ('g' == idstr[0] ? token::grid : token::margin);
					}
					else if ("collapse" == idstr_)
					{
						if (token::parameters != read())
							_m_throw_error("a parameter list is required after 'collapse'");
						return token::collapse;
					}
					return token::identifier;
				}

				std::string err = "an invalid character '";
				err += *sp_;
				err += "'";

				_m_throw_error(err);
				return token::error;	//Useless, just for syntax correction.
			}
		private:
			void _m_attr_number_value()
			{
				if (token::equal != read())
					_m_throw_error("an equal sign is required after '" + idstr_ + "'");

				auto p = _m_eat_whitespace(sp_);

				auto neg_ptr = p;
				if ('-' == *p)
					++p;

				auto len = _m_number(p, neg_ptr != p);
				if (0 == len)
					_m_throw_error("the '" + idstr_ + "' requires a number(integer or real or percent)");

				sp_ += len + (p - sp_);
			}

			void _m_attr_reparray()
			{
				auto idstr = idstr_;
				if (token::equal != read())
					_m_throw_error("an equal sign is required after '" + idstr + "'");

				const char* p = _m_eat_whitespace(sp_);

				reparray_.reset();
				auto tk = read();
				switch (tk)
				{
				case token::number:
					reparray_.push(number());
					reparray_.repeated();
					break;
				case token::array:
					reparray_.assign(std::move(array_));
					break;
				case token::reparray:
					break;
				default:
					_m_throw_error("a (repeated) array is required after '" + idstr + "'");
				}
			}

			void _m_throw_error(const std::string& err)
			{
				throw std::invalid_argument("nana.place: " + err + " at " + std::to_string(sp_ - divstr_));
			}

			const char* _m_eat_whitespace(const char* sp)
			{
				while (*sp && !isgraph(*sp))
					++sp;
				return sp;
			}

			std::size_t _m_number(const char* sp, bool negative)
			{
				const char* allstart = sp;
				sp = _m_eat_whitespace(sp);

				number_.assign(0);

				bool gotcha = false;
				int integer = 0;
				double real = 0;
				//read the integral part.
				const char* istart = sp;
				while ('0' <= *sp && *sp <= '9')
				{
					integer = integer * 10 + (*sp - '0');
					++sp;
				}
				const char* iend = sp;

				if ('.' == *sp)
				{
					double div = 1;
					const char* rstart = ++sp;
					while ('0' <= *sp && *sp <= '9')
					{
						real += (*sp - '0') / (div *= 10);
						++sp;
					}

					if (rstart != sp)
					{
						real += integer;
						number_.assign(negative ? -real : real);
						gotcha = true;
					}
				}
				else if (istart != iend)
				{
					number_.assign(negative ? -integer : integer);
					gotcha = true;
				}

				if (gotcha)
				{
					sp = _m_eat_whitespace(sp);
					if ('%' == *sp)
					{
						if (number_t::kind::integer == number_.kind_of())
							number_.assign_percent(number_.integer());
						return sp - allstart + 1;
					}
					return sp - allstart;
				}
				number_.reset();
				return 0;
			}
		private:
			const char* divstr_;
			const char* sp_;
			std::string idstr_;
			number_t number_;
			std::vector<number_t> array_;
			repeated_array		reparray_;
			std::vector<number_t> parameters_;
		};	//end class tokenizer
	}


	//struct implement
	struct place::implement
	{
		class field_impl;
		class division;
		class div_arrange;
		class div_grid;
		class div_splitter;
		class div_dock;

		window window_handle{nullptr};
		event_handle event_size_handle{nullptr};
		std::unique_ptr<division> root_division;
		std::map<std::string, field_impl*> fields;

		//A temporary pointer used to refer to a specified div object which
		//will be deleted in modification process.
		std::unique_ptr<division> tmp_replaced;

		//The following functions are defined behind the definition of class division.
		//because the class division here is an incomplete type.
		~implement();

		void collocate();

		static division * search_div_name(division* start, const std::string&);
		std::unique_ptr<division> scan_div(place_parts::tokenizer&);
		void check_unique(const division*) const;
	};	//end struct implement

	class place::implement::field_impl
		: public place::field_interface
	{
	public:
		struct element_t
		{
			window handle;
			event_handle evt_destroy;

			element_t(window h, event_handle event_destroy)
				:handle(h), evt_destroy(event_destroy)
			{}
		};

		field_impl(place * p)
			: place_ptr_(p)
		{}

		~field_impl()
		{
			for (auto & e : elements)
				API::umake_event(e.evt_destroy);

			for (auto & e : fastened)
				API::umake_event(e.evt_destroy);
		}

		void visible(bool vsb)
		{
			for (auto & e : elements)
				API::show_window(e.handle, vsb);

			for (auto & e : fastened)
				API::show_window(e.handle, vsb);
		}
	private:
		//The defintion is moved after the definition of class division
		template<typename Function>
		void _m_for_each(division*, Function);

		//Listen to destroy of a window
		//It will delete the element and recollocate when the window destroyed.
		event_handle _m_make_destroy(window wd)
		{
			return API::events(wd).destroy.connect([this](const arg_destroy& arg)
			{
				for (auto i = elements.begin(), end = elements.end(); i != end; ++i)
				{
					if (arg.window_handle == i->handle)
					{
						elements.erase(i);
						break;
					}
				}
				place_ptr_->collocate();
			});
		}

		field_interface& operator<<(const nana::char_t* label_text) override
		{
			return static_cast<field_interface*>(this)->operator<<(agent<label>(label_text ? label_text : L""));
		}

		virtual field_interface& operator<<(nana::string label_text) override
		{
			return static_cast<field_interface*>(this)->operator<<(agent<label>(label_text));
		}

		field_interface& operator<<(window wd) override
		{
			if (API::empty_window(wd))
				throw std::invalid_argument("Place: An invalid window handle.");

			if (API::get_parent_window(wd) != place_ptr_->window_handle())
				throw std::invalid_argument("Place: the window is not a child of place bind window");

			auto evt = _m_make_destroy(wd);
			elements.emplace_back(wd, evt);
			return *this;
		}

		field_interface& fasten(window wd) override
		{
			if (API::empty_window(wd))
				throw std::invalid_argument("Place: An invalid window handle.");

			if (API::get_parent_window(wd) != place_ptr_->window_handle())
				throw std::invalid_argument("Place: the window is not a child of place bind window");

			//Listen to destroy of a window. The deleting a fastened window
			//does not change the layout.
			auto evt = API::events(wd).destroy([this](const arg_destroy& arg)
			{
				auto destroyed_wd = arg.window_handle;
				auto i = std::find_if(fastened.begin(), fastened.end(), [destroyed_wd](element_t& e){
					return (e.handle == destroyed_wd);
				});

				if (i != fastened.end())
					fastened.erase(i);
			});

			fastened.emplace_back(wd, evt);
			return *this;
		}

		void _m_add_agent(const detail::place_agent& ag) override
		{
			widgets_.emplace_back(ag.create(place_ptr_->window_handle()));
			this->operator<<(widgets_.back()->handle());
		}
	public:
		division* attached{ nullptr };
		std::vector<std::unique_ptr<nana::widget>> widgets_;
		std::vector<element_t>	elements;
		std::vector<element_t>	fastened;
	private:
		place * place_ptr_;
	};//end class field_impl



	class place::implement::division
	{
	public:
		enum class kind{ arrange, vertical_arrange, grid, splitter, dock };

		division(kind k, std::string&& n)
			: kind_of_division(k),
			name(std::move(n)),
			field(nullptr),
			div_next(nullptr),
			div_owner(nullptr)
		{}

		virtual ~division()
		{
			//detach the field
			if (field)
				field->attached = nullptr;
		}

		void set_visible(bool vsb)
		{
			if (field)
				field->visible(vsb);

			_m_visible_for_child(this, vsb);
			visible = vsb;
		}

		void set_display(bool dsp)
		{
			if (field)
				field->visible(dsp);

			_m_visible_for_child(this, dsp);
			visible = dsp;
			display = dsp;

			if (kind::splitter != kind_of_division)
			{
				//Don't display the previous one, if it is a splitter
				auto div = previous();
				if (div && (kind::splitter == div->kind_of_division))
				{
					if (dsp)
					{
						auto leaf = div->previous();
						if (leaf && leaf->display)
							div->set_display(true);
					}
					else
						div->set_display(false);
				}

				//Don't display the next one, if it is a splitter
				if (div_next && (kind::splitter == div_next->kind_of_division))
				{
					if (dsp)
					{
						auto leaf = div_next->div_next;
						if (leaf && leaf->display)
							div_next->set_display(true);
					}
					else
						div_next->set_display(false);
				}
			}
			else
			{
				//This is a splitter, it only checks when it is being displayed
				if (dsp)
				{
					auto left = this->previous();
					if (left && !left->display)
						left->set_display(true);

					auto right = div_next;
					if (right && !right->display)
						right->set_display(true);
				}
			}
		}

		bool is_back(const division* div) const
		{
			for (auto i = children.crbegin(); i != children.crend(); ++i)
			{
				if (!(i->get()->display))
					continue;

				return (div == i->get());
			}
			return false;
		}

		static double limit_px(const division* div, double px, unsigned area_px)
		{
			if (div->min_px.is_not_none())
			{
				auto v = div->min_px.get_value(static_cast<int>(area_px));
				if (px < v)
					return v;
			}
			if (div->max_px.is_not_none())
			{
				auto v = div->max_px.get_value(static_cast<int>(area_px));
				if (px > v)
					return v;
			}
			return px;
		}

		bool is_fixed() const
		{
			return (weight.kind_of() == number_t::kind::integer);
		}

		bool is_percent() const
		{
			return (weight.kind_of() == number_t::kind::percent);
		}

		nana::rectangle margin_area() const
		{
			return margin.area(field_area);
		}

		division * previous() const
		{
			if (div_owner)
			{
				for (auto & child : div_owner->children)
					if (child->div_next == this)
						return child.get();
			}
			return nullptr;
		
		}
	public:
		static void _m_visible_for_child(division * div, bool vsb)
		{
			for (auto & child : div->children)
			{
				if (child->field)
				{
					if ((!vsb) || child->visible)
						child->field->visible(vsb);
				}
				_m_visible_for_child(child.get(), vsb);
			}
		}
		//Collocate the division and its children divisions,
		//The window parameter is specified for the window which the place object binded.
		virtual void collocate(window) = 0;

	public:
		kind kind_of_division;
		bool display{ true };
		bool visible{ true };
		std::string name;
		std::vector<std::unique_ptr<division>> children;

		::nana::rectangle field_area;
		number_t weight;
		number_t min_px, max_px;

		place_parts::margin	margin;
		place_parts::repeated_array gap;
		field_impl * field;
		division * div_next, *div_owner;
	};//end class division

	template<typename Function>
	void place::implement::field_impl::_m_for_each(division * div, Function fn)
	{
		for (auto & up : div->children)	//The element of children is unique_ptr
			fn(up.get());
	}


	class place::implement::div_arrange
		: public division
	{
	public:
		div_arrange(bool vert, std::string&& name, place_parts::repeated_array&& arr)
			: division((vert ? kind::vertical_arrange : kind::arrange), std::move(name)),
			arrange_(std::move(arr))
		{}

		void collocate(window wd) override
		{
			const bool vert = (kind::arrange != kind_of_division);

			auto area_margined = margin_area();
			area_rotator area(vert, area_margined);
			auto area_px = area.w();

			auto fa = _m_fixed_and_adjustable(kind_of_division, area_px);

			double adjustable_px = _m_revise_adjustable(fa, area_px);

			double position = area.x();
			std::vector<division*> delay_collocates;
			double precise_px = 0;
			for (auto& child_ptr : children)					/// First collocate child div's !!!
			{
				auto child = child_ptr.get();
				if(!child->display)	//Ignore the division if the corresponding field is not displayed.
					continue;

				area_rotator child_area(vert, child->field_area);
				child_area.x_ref() = static_cast<int>(position);
				child_area.y_ref() = area.y();
				child_area.h_ref() = area.h();

				double child_px;							/// and calculate this div.
				if (!child->is_fixed())					/// with is fixed for fixed div
				{
					if (child->is_percent())			/// and calculated for others: if the child div is percent - simple take it full
						child_px = area_px * child->weight.real() + precise_px;
					else
						child_px = adjustable_px;

					child_px = limit_px(child, child_px, area_px);
					auto npx = static_cast<unsigned>(child_px);
					precise_px = child_px - npx;
					child_px = npx;
				}
				else
					child_px = static_cast<unsigned>(child->weight.integer());

				//Use 'endpos' to calc width is to avoid deviation
				int endpos = static_cast<int>(position + child_px);
				if ((!child->is_fixed()) && child->max_px.is_none() && is_back(child) && (endpos != area.right()))
					endpos = area.right();

				child_area.w_ref() = static_cast<unsigned>(endpos - child_area.x());

				child->field_area = child_area.result();
				position += child_px;

				if (child->kind_of_division == kind::splitter)
					delay_collocates.emplace_back(child);
				else
					child->collocate(wd);	// The child div have full position. Now we can collocate  inside it the child fields and child-div.
			}

			for (auto child : delay_collocates)
				child->collocate(wd);

			if (visible && display && field)
			{
				auto element_r = area;
				std::size_t index = 0;
				double precise_px = 0;

				for (auto & el : field->elements)
				{
					element_r.x_ref() = static_cast<int>(position);
					auto px = _m_calc_number(arrange_.at(index), area_px, adjustable_px, precise_px);

					element_r.w_ref() = px;
					API::move_window(el.handle, element_r.result());

					if (index + 1 < field->elements.size())
					{
						position += px;
						position += _m_calc_number(gap.at(index), area_px, 0, precise_px);
					}
					++index;
				}

				for (auto & fsn : field->fastened)
					API::move_window(fsn.handle, area_margined);
			}
		}
	private:
		unsigned _m_calc_number(const place_parts::number_t& number, unsigned area_px, double adjustable_px, double& precise_px)
		{
			switch (number.kind_of())
			{
			case number_t::kind::integer:
				return static_cast<unsigned>(number.integer());
			case number_t::kind::real:
				return static_cast<unsigned>(number.real());
			case number_t::kind::percent:
				adjustable_px = area_px * number.real();
			case number_t::kind::none:
				{
					auto fpx = adjustable_px + precise_px;
					auto px = static_cast<unsigned>(fpx);
					precise_px = fpx - px;
					return px;
				}
				break;
			}
			return 0; //Useless
		}

		std::pair<unsigned, std::size_t> _m_calc_fa(const place_parts::number_t& number, unsigned area_px, double& precise_px) const
		{
			std::pair<unsigned, std::size_t> result;
			switch (number.kind_of())
			{
			case number_t::kind::integer:
				result.first = static_cast<unsigned>(number.integer());
				break;
			case number_t::kind::real:
				result.first = static_cast<unsigned>(number.real());
				break;
			case number_t::kind::percent:
				{
					double px = number.real() * area_px + precise_px;
					auto npx = static_cast<unsigned>(px);
					result.first = npx;
					precise_px = px - npx;
				}
				break;
			case number_t::kind::none:
				++result.second;
				break;
			}
			return result;
		}

		//Returns the fixed pixels and the number of adjustable items.
		std::pair<unsigned, std::size_t> _m_fixed_and_adjustable(kind match_kind, unsigned area_px) const
		{
			std::pair<unsigned, std::size_t> result;
			if (field && (kind_of_division == match_kind))
			{
				//Calculate fixed and adjustable of elements
				double precise_px = 0;
				auto count = field->elements.size();
				for (decltype(count) i = 0; i < count; ++i)
				{
					auto fa = _m_calc_fa(arrange_.at(i), area_px, precise_px);
					result.first += fa.first;
					result.second += fa.second;

					if (i + 1 < count)
					{
						fa = _m_calc_fa(gap.at(i), area_px, precise_px);
						result.first += fa.first;
						//fa.second is ignored for gap, because the it has not the adjustable gap.
					}
				}
			}

			double children_fixed_px = 0;
			for (auto& child : children)
			{
				if (!child->display)	//Ignore the division if the corresponding field is not displayed.
					continue;

				if (child->weight.is_not_none())
					children_fixed_px += child->weight.get_value(area_px);
				else
					++result.second;
			}
			result.first += static_cast<unsigned>(children_fixed_px);
			return result;
		}

		double _m_revise_adjustable(std::pair<unsigned, std::size_t>& fa, unsigned area_px)
		{
			if (fa.first >= area_px || 0 == fa.second)
				return 0;

			double var_px = area_px - fa.first;

			struct revise_t
			{
				division * div;
				double min_px;
				double max_px;
			};

			std::size_t min_count = 0;
			double sum_min_px = 0;
			std::vector<revise_t> revises;
			for (auto& child : children)
			{
				if (child->weight.is_not_none() || !child->display)
					continue;

				double min_px = std::numeric_limits<double>::lowest(), max_px = std::numeric_limits<double>::lowest();

				if (child->min_px.is_not_none())
				{
					min_px = child->min_px.get_value(static_cast<int>(area_px));
					sum_min_px += min_px;
					++min_count;
				}

				if (child->max_px.is_not_none())
					max_px = child->max_px.get_value(static_cast<int>(area_px));

				if (min_px >= 0 && max_px >= 0 && min_px > max_px)
				{
					if (child->min_px.kind_of() == number_t::kind::percent)
						min_px = std::numeric_limits<double>::lowest();
					else if (child->max_px.kind_of() == number_t::kind::percent)
						max_px = std::numeric_limits<double>::lowest();
				}

				if (min_px >= 0 || max_px >= 0)
					revises.push_back({ child.get(), min_px, max_px });
			}

			if (revises.empty())
				return var_px / fa.second;

			auto find_lowest = [&revises](double level_px)
			{
				double v = std::numeric_limits<double>::max();
				for (auto i = revises.begin(); i != revises.end(); ++i)	//deprecated
				{
					if (i->min_px >= 0 && i->min_px < v && i->min_px > level_px)
						v = i->min_px;
					else if (i->max_px >= 0 && i->max_px < v)
						v = i->max_px;
				}
				return v;
			};

			auto remove_full = [&revises](double value, std::size_t& full_count)
			{
				full_count = 0;
				std::size_t reached_mins = 0;
				auto i = revises.begin();
				while(i != revises.end())
				{
					if (i->max_px == value)
					{
						++full_count;
						i = revises.erase(i);
					}
					else
					{
						if (i->min_px == value)
							++reached_mins;
						++i;
					}
				}
				return reached_mins;
			};

			double block_px = 0;
			double level_px = 0;
			auto rest_px = var_px - sum_min_px;
			std::size_t blocks = fa.second;

			while ((rest_px > 0) && blocks)
			{
				auto lowest = find_lowest(level_px);
				double fill_px = 0;
				//blocks may be equal to min_count. E.g, all child divisions have min/max attribute.
				if (blocks > min_count)
				{
					fill_px = rest_px / (blocks - min_count);
					if (fill_px + level_px <= lowest)
					{
						block_px += fill_px;
						break;
					}
				}

				block_px = lowest;

				if (blocks > min_count)
					rest_px -= (lowest-level_px) * (blocks - min_count);

				std::size_t full_count;
				min_count -= remove_full(lowest, full_count);
				blocks -= full_count;
				level_px = lowest;
			}

			return block_px;
		}
	private:
		place_parts::repeated_array arrange_;
	};

	class place::implement::div_grid
		: public division
	{
	public:
		div_grid(std::string&& name, place_parts::repeated_array&& arrange, std::vector<rectangle>&& collapses)
			: division(kind::grid, std::move(name)),
			arrange_(std::move(arrange)),
			collapses_(std::move(collapses))
		{
			dimension.first = dimension.second = 0;
		}

		void revise_collapses()
		{
			if (collapses_.empty())
				return;

			for (auto i = collapses_.begin(); i != collapses_.end();)
			{
				if (i->x >= static_cast<int>(dimension.first) || i->y >= static_cast<int>(dimension.second))
					i = collapses_.erase(i);
				else
					++i;
			}

			//Remove the overlapped collapses
			for (std::size_t i = 0; i < collapses_.size() - 1; ++i)
			{
				auto & col = collapses_[i];
				for (auto u = i + 1; u != collapses_.size();)
				{
					auto & z = collapses_[u];
					if (col.is_hit(z.x, z.y) || col.is_hit(z.right(), z.y) || col.is_hit(z.x, z.bottom()) || col.is_hit(z.right(), z.bottom()))
						collapses_.erase(collapses_.begin() + u);
					else
						++u;
				}
			}

			for (auto & col : collapses_)
			{
				if (col.right() >= static_cast<int>(dimension.first))
					col.width = dimension.first - static_cast<unsigned>(col.x);

				if (col.bottom() >= static_cast<int>(dimension.second))
					col.height = dimension.second - static_cast<unsigned>(col.y);
			}
		}

		void collocate(window wd) override
		{

			if (!field || !(visible && display))
				return;

			auto area = margin_area();

			unsigned gap_size = 0;
			auto gap_number = gap.at(0);
			if (gap_number.is_not_none())
			{
				gap_size = (gap_number.kind_of() == number_t::kind::percent ?
					static_cast<unsigned>(area.width * gap_number.real()) : static_cast<unsigned>(gap_number.integer()));
			}

			//When the amount pixels of gaps is out of the area bound.
			if ((gap_size * dimension.first >= area.width) || (gap_size * dimension.second >= area.height))
			{
				for (auto & el : field->elements)
					API::window_size(el.handle, size{ 0, 0 });
				return;
			}

			if (dimension.first <= 1 && dimension.second <= 1)
			{
				auto n_of_wd = field->elements.size();
				std::size_t edge;
				switch (n_of_wd)
				{
				case 0:
				case 1:
					edge = 1;	break;
				case 2: case 3: case 4:
					edge = 2;	break;
				default:
					edge = static_cast<std::size_t>(std::sqrt(n_of_wd));
					if ((edge * edge) < n_of_wd) ++edge;
				}

				bool exit_for = false;
				double y = area.y;
				double block_w = area.width / double(edge);
				double block_h = area.height / double((n_of_wd / edge) + (n_of_wd % edge ? 1 : 0));
				unsigned uns_block_w = static_cast<unsigned>(block_w);
				unsigned uns_block_h = static_cast<unsigned>(block_h);
				unsigned height = (uns_block_h > gap_size ? uns_block_h - gap_size : uns_block_h);

				auto i = field->elements.cbegin(), end = field->elements.cend();
				std::size_t arr_pos = 0;
				for (std::size_t u = 0; u < edge; ++u)
				{
					double x = area.x;
					for (std::size_t v = 0; v < edge; ++v)
					{
						if (i == end)
						{
							exit_for = true;
							break;
						}

						unsigned value = 0;
						auto arr = arrange_.at(arr_pos++);

						if (arr.is_none())
							value = static_cast<decltype(value)>(block_w);
						else
							value = static_cast<decltype(value)>(arr.get_value(static_cast<int>(area.width)));

						unsigned width = (value > uns_block_w ? uns_block_w : value);
						if (width > gap_size)	width -= gap_size;
						API::move_window(i->handle, rectangle{ static_cast<int>(x), static_cast<int>(y), width, height });
						x += block_w;
						++i;
					}
					if (exit_for) break;
					y += block_h;
				}
			}
			else
			{
				double block_w = int(area.width - gap_size * (dimension.first - 1)) / double(dimension.first);
				double block_h = int(area.height - gap_size * (dimension.second - 1)) / double(dimension.second);

				std::unique_ptr<char[]> table_ptr(new char[dimension.first * dimension.second]);

				char *table = table_ptr.get();
				std::memset(table, 0, dimension.first * dimension.second);

				std::size_t lbp = 0;
				bool exit_for = false;

				auto i = field->elements.cbegin(), end = field->elements.cend();

				double precise_h = 0;
				for (std::size_t c = 0; c < dimension.second; ++c)
				{
					unsigned block_height_px = static_cast<unsigned>(block_h + precise_h);
					precise_h = (block_h + precise_h) - block_height_px;

					double precise_w = 0;
					for (std::size_t l = 0; l < dimension.first; ++l)
					{
						if (table[l + lbp])
						{
							precise_w += block_w;
							auto px = static_cast<int>(precise_w);
							precise_w -= px;
							continue;
						}

						if (i == end)
						{
							exit_for = true;
							break;
						}

						std::pair<unsigned, unsigned> room{ 1, 1 };

						_m_find_collapse(static_cast<int>(l), static_cast<int>(c), room);

						int pos_x = area.x + static_cast<int>(l * (block_w + gap_size));
						int pos_y = area.y + static_cast<int>(c * (block_h + gap_size));

						unsigned result_h;
						if (room.first <= 1 && room.second <= 1)
						{
							precise_w += block_w;
							result_h = block_height_px;
							table[l + lbp] = 1;
						}
						else
						{
							precise_w += block_w * room.first + (room.first - 1) * gap_size;
							result_h = static_cast<unsigned>(block_h * room.second + precise_h + (room.second - 1) * gap_size);

							for (unsigned y = 0; y < room.second; ++y)
								for (unsigned x = 0; x < room.first; ++x)
									table[l + x + lbp + y * dimension.first] = 1;
						}

						unsigned result_w = static_cast<unsigned>(precise_w);
						precise_w -= result_w;

						API::move_window(i->handle, rectangle{ pos_x, pos_y, result_w, result_h });
						++i;
					}

					if (exit_for)
					{
						size empty_sz;
						for (; i != end; ++i)
							API::window_size(i->handle, empty_sz);
						break;
					}
					lbp += dimension.first;
				}
			}

			for (auto & fsn : field->fastened)
				API::move_window(fsn.handle, area);
		}
	public:
		std::pair<unsigned, unsigned> dimension;
	private:
		void _m_find_collapse(int x, int y, std::pair<unsigned, unsigned>& collapse) const
		{
			for (auto & col : collapses_)
			{
				if (col.x == x && col.y == y)
				{
					collapse.first = col.width;
					collapse.second = col.height;
					return;
				}
			}
		}
	private:
		place_parts::repeated_array arrange_;
		std::vector<rectangle> collapses_;
	};//end class div_grid

	class place::implement::div_splitter
		: public division
	{
		struct div_block
		{
			division * div;
			int	pixels;
			double		scale;

			div_block(division* d, int px)
				: div(d), pixels(px)
			{}
		};

		enum{splitter_px = 4};
	public:
		div_splitter(place_parts::number_t init_weight)
			: division(kind::splitter, std::string()),
			splitter_cursor_(cursor::arrow),
			pause_move_collocate_(false),
			init_weight_(init_weight)
		{
			this->weight.assign(splitter_px);
		}

		void direction(bool horizontal)
		{
			splitter_cursor_ = (horizontal ? cursor::size_we : cursor::size_ns);
		}
	private:
		void collocate(window wd) override
		{
			if (splitter_.empty())
			{
				splitter_.create(wd);
				splitter_.cursor(splitter_cursor_);

				dragger_.trigger(splitter_);
				splitter_.events().mouse_down.connect_unignorable([this](const arg_mouse& arg)
				{
					if (false == arg.left_button)
						return;

					begin_point_ = splitter_.pos();

					auto px_ptr = &nana::rectangle::width;

					auto area_left = _m_leaf_left()->margin_area();
					auto area_right = _m_leaf_right()->margin_area();

					if (nana::cursor::size_we != splitter_cursor_)
					{
						left_pos_ = area_left.y;
						right_pos_ = area_right.bottom();
						px_ptr = &nana::rectangle::height;
					}
					else
					{
						left_pos_ = area_left.x;
						right_pos_ = area_right.right();
					}

					left_pixels_ = area_left.*px_ptr;
					right_pixels_ = area_right.*px_ptr;
				});

				splitter_.events().mouse_move.connect_unignorable([this](const arg_mouse& arg)
				{
					if (false == arg.left_button)
						return;

					const bool vert = (::nana::cursor::size_we != splitter_cursor_);
					auto area_px = area_rotator(vert, div_owner->margin_area()).w();
					int delta = (vert ? splitter_.pos().y - begin_point_.y : splitter_.pos().x - begin_point_.x);

					int total_pixels = static_cast<int>(left_pixels_ + right_pixels_);

					auto left_px = static_cast<int>(left_pixels_)+delta;
					if (left_px > total_pixels)
						left_px = total_pixels;
					else if (left_px < 0)
						left_px = 0;

					auto leaf_left = _m_leaf_left();
					double imd_rate = 100.0 / area_px;
					left_px = static_cast<int>(limit_px(leaf_left, left_px, area_px));
					leaf_left->weight.assign_percent(imd_rate * left_px);

					auto right_px = static_cast<int>(right_pixels_)-delta;
					if (right_px > total_pixels)
						right_px = total_pixels;
					else if (right_px < 0)
						right_px = 0;

					auto leaf_right = _m_leaf_right();
					right_px = static_cast<int>(limit_px(leaf_right, right_px, area_px));
					leaf_right->weight.assign_percent(imd_rate * right_px);

					pause_move_collocate_ = true;
					div_owner->collocate(splitter_.parent());

					//After the collocating, the splitter keeps the calculated weight of left division,
					//and clear the weight of right division.
					leaf_right->weight.reset();

					pause_move_collocate_ = false;
				});
			}

			auto limited_range = _m_update_splitter_range();

			if (init_weight_.is_not_none())
			{
				const bool vert = (::nana::cursor::size_we != splitter_cursor_);

				auto leaf_left = _m_leaf_left();
				auto leaf_right = _m_leaf_right();
				area_rotator left(vert, leaf_left->field_area);
				area_rotator right(vert, leaf_right->field_area);
				auto area_px = right.right() - left.x();
				auto right_px = static_cast<int>(limit_px(leaf_right, init_weight_.get_value(area_px), static_cast<unsigned>(area_px)));

				auto pos = area_px - right_px - splitter_px; //New position of splitter
				if (pos < limited_range.x())
					pos = limited_range.x();
				else if (pos > limited_range.right())
					pos = limited_range.right();

				area_rotator sp_r(vert, field_area);
				sp_r.x_ref() = pos;

				left.w_ref() = static_cast<unsigned>(pos - left.x());

				auto right_pos = right.right();
				right.x_ref() = (pos + splitter_px);
				right.w_ref() = static_cast<unsigned>(right_pos - pos - splitter_px);

				field_area = sp_r.result();
				leaf_left->field_area = left.result();
				leaf_right->field_area = right.result();
				leaf_left->collocate(wd);
				leaf_right->collocate(wd);

				//Set the leafs' weight
				area_rotator area(vert, div_owner->field_area);

				double imd_rate = 100.0 / static_cast<int>(area.w());
				leaf_left->weight.assign_percent(imd_rate * static_cast<int>(left.w()));
				leaf_right->weight.assign_percent(imd_rate * static_cast<int>(right.w()));

				splitter_.move(this->field_area);

				init_weight_.reset();
			}

			if (false == pause_move_collocate_)
				splitter_.move(this->field_area);
		}
	private:
		division * _m_leaf_left() const
		{
			return previous();
		}

		division * _m_leaf_right() const
		{
			return div_next;
		}

		area_rotator _m_update_splitter_range()
		{
			const bool vert = (cursor::size_ns == splitter_cursor_);

			area_rotator area(vert, div_owner->margin_area());

			auto leaf_left = _m_leaf_left();
			auto leaf_right = _m_leaf_right();

			area_rotator left(vert, leaf_left->field_area);
			area_rotator right(vert, leaf_right->field_area);

			const int left_base = left.x(), right_base = right.right();
			int pos = left_base;
			int endpos = right_base;

			if (leaf_left->min_px.is_not_none())
			{
				auto v = leaf_left->min_px.get_value(area.w());
				pos += static_cast<int>(v);
			}
			if (leaf_left->max_px.is_not_none())
			{
				auto v = leaf_left->max_px.get_value(area.w());
				endpos = left_base + static_cast<int>(v);
			}

			if (leaf_right->min_px.is_not_none())
			{
				auto v = leaf_right->min_px.get_value(area.w());
				auto x = right_base - static_cast<int>(v);
				if (x < endpos)
					endpos = x;
			}
			if (leaf_right->max_px.is_not_none())
			{
				auto v = leaf_right->max_px.get_value(area.w());
				auto x = right_base - static_cast<int>(v);
				if (x > pos)
					pos = x;
			}

			area.x_ref() = pos;
			area.w_ref() = unsigned(endpos - pos + splitter_px);

			dragger_.target(splitter_, area.result(), (vert ? nana::arrange::vertical : nana::arrange::horizontal));

			return area;
		}
	private:
		nana::cursor	splitter_cursor_;
		place_parts::splitter<true>	splitter_;
		nana::point	begin_point_;
		int			left_pos_, right_pos_;
		unsigned	left_pixels_, right_pixels_;
		dragger	dragger_;
		bool	pause_move_collocate_;	//A flag represents whether do move when collocating.
		place_parts::number_t init_weight_;
	};

	class place::implement::div_dock
		: public division, public place_parts::dock_notifier_interface
	{
	public:
		div_dock(std::string && name, implement* impl)
			:	division(kind::dock, std::move(name)),
				impl_ptr_{impl}
		{}

		void collocate(window wd) override
		{
			if (dockarea_.empty())
				dockarea_.create(wd, this);

			if (!dockarea_.floating())
			{
				dockarea_.move(this->field_area);
				indicator_.r = this->field_area;
			}
		}

		place_parts::dockarea& dockarea()
		{
			return dockarea_;
		}
	private:
		//Implement dock_notifier_interface
		void notify_float() override
		{
			set_display(false);

			impl_ptr_->collocate();
		}

		void notify_dock() override
		{
			indicator_.docker.reset();
			set_display(true);
			impl_ptr_->collocate();
		}

		void notify_move() override
		{
			if (!_m_indicator())
			{
				indicator_.docker.reset();
				return;
			}

			if (!indicator_.docker)
			{
				auto host_size = API::window_size(impl_ptr_->window_handle);
				indicator_.docker.reset(new form(impl_ptr_->window_handle, { static_cast<int>(host_size.width) / 2 - 16, static_cast<int>(host_size.height) / 2 - 16, 32, 32 }, form::appear::bald<>()));
				drawing dw(indicator_.docker->handle());
				dw.draw([](paint::graphics& graph)
				{
					graph.rectangle(false, colors::midnight_blue);
					graph.rectangle({ 1, 1, 30, 30 }, true, colors::light_sky_blue);

					facade<element::arrow> arrow;
					arrow.direction(::nana::direction::south);
					arrow.draw(graph, colors::light_sky_blue, colors::midnight_blue, { 12, 0, 16, 16 }, element_state::normal);

					rectangle r{4, 16, 24, 11};
					graph.rectangle(r, true, colors::midnight_blue);

					r.x = 5;
					r.y = 19;
					r.width = 22;
					r.height = 7;
					graph.rectangle(r, true, colors::button_face);
				});

				indicator_.docker->z_order(nullptr, ::nana::z_order_action::topmost);
				indicator_.docker->show();

				indicator_.docker->events().destroy([this]
				{
					if (indicator_.dock_area)
					{
						indicator_.dock_area.reset();
						indicator_.graph.release();
					}
				});
			}

			if (_m_dockable())
			{
				if (!indicator_.dock_area)
				{
					indicator_.graph.make(API::window_size(impl_ptr_->window_handle));
					API::window_graphics(impl_ptr_->window_handle, indicator_.graph);

					//indicator_.dock_area.reset(new form(impl_ptr_->window_handle, ::nana::size(), form::appear::bald<>()));	//deprecated
					indicator_.dock_area.reset(new panel<true>(impl_ptr_->window_handle, false));
					indicator_.dock_area->move(indicator_.r);

					::nana::drawing dw(indicator_.dock_area->handle());
					dw.draw([this](paint::graphics& graph)
					{
						indicator_.graph.paste(indicator_.r, graph, 0, 0);

						const int border_px = 4;
						rectangle r = graph.size();
						int right = r.right();
						int bottom = r.bottom();

						graph.blend(r.pare_off(border_px), colors::blue, 0.3);

						::nana::color clr = colors::deep_sky_blue;
						r.y = 0;
						r.height = border_px;
						graph.blend(r, clr, 0.5);
						r.y = bottom - border_px;
						graph.blend(r, clr, 0.5);

						r.x = r.y = 0;
						r = graph.size();
						r.width = border_px;
						graph.blend(r, clr, 0.5);
						r.x = right - border_px;
						graph.blend(r, clr, 0.5);

					});
					//indicator_.dock_area->z_order(nullptr, ::nana::z_order_action::top);	//deprecated
					API::bring_top(indicator_.dock_area->handle(), false);
					indicator_.dock_area->show();
				}
			}
			else
			{
				if (indicator_.dock_area)
				{
					indicator_.dock_area.reset();
					indicator_.graph.release();
				}
			}

		}

		void notify_move_stopped()
		{
			if (_m_dockable())
				dockarea_.dock();

			indicator_.docker.reset();
		}
	private:
		bool _m_indicator() const
		{
			::nana::point pos;
			API::calc_screen_point(impl_ptr_->window_handle, pos);

			rectangle r{ pos, API::window_size(impl_ptr_->window_handle) };
			return r.is_hit(API::cursor_position());
		}

		bool _m_dockable() const
		{
			if (!indicator_.docker)
				return false;

			::nana::point pos;
			API::calc_screen_point(indicator_.docker->handle(), pos);

			rectangle r{ pos, API::window_size(indicator_.docker->handle()) };
			return r.is_hit(API::cursor_position());
		}
	private:
		implement * const impl_ptr_;
		place_parts::dockarea dockarea_;

		struct indicator_tag
		{
			paint::graphics graph;
			//panel<true> widget;
			rectangle	r;
			std::unique_ptr<panel<true>> dock_area;
			std::unique_ptr<form> docker;
		}indicator_;
	};

	place::implement::~implement()
	{
		API::umake_event(event_size_handle);
		root_division.reset();
		for (auto & pair : fields)
			delete pair.second;
	}

	void place::implement::collocate()
	{
		if (root_division && window_handle)
		{
			root_division->field_area = API::window_size(window_handle);

			if (root_division->field_area.empty())
				return;

			root_division->collocate(window_handle);

			for (auto & field : fields)
			{
				bool is_show = false;
				if (field.second->attached && field.second->attached->visible && field.second->attached->display)
				{
					is_show = true;
					auto div = field.second->attached->div_owner;
					while (div)
					{
						if (!div->visible || !div->display)
						{
							is_show = false;
							break;
						}
						div = div->div_owner;
					}
				}

				for (auto & el : field.second->elements)
					API::show_window(el.handle, is_show);
			}
		}
	}

	//search_div_name
	//search a division with the specified name.
	place::implement::division * place::implement::search_div_name(division* start, const std::string& name)
	{
		if (start)
		{
			if (start->name == name)
				return start;

			for (auto& child : start->children)
			{
				auto div = search_div_name(child.get(), name);
				if (div)
					return div;
			}
		}
		return nullptr;
	}

	auto place::implement::scan_div(place_parts::tokenizer& tknizer) -> std::unique_ptr<division>
	{
		typedef place_parts::tokenizer::token token;

		std::unique_ptr<division> div;
		token div_type = token::eof;

		//These variables stand for the new division's attributes
		std::string name;
		number_t weight, min_px, max_px;
		place_parts::repeated_array arrange, gap;
		place_parts::margin margin;
		std::vector<number_t> array;
		std::vector<rectangle> collapses;
		std::vector<std::unique_ptr<division>> children;

		for (token tk = tknizer.read(); tk != token::eof; tk = tknizer.read())
		{
			bool exit_for = false;
			switch (tk)
			{
			case token::dock:
				if (token::eof != div_type && token::dock != div_type)
					throw std::invalid_argument("nana.place: conflict of div type at " + tknizer.pos_str());

				div_type = token::dock;
				break;
			case token::splitter:
				//Ignore the splitter when there is not a division.
				if (!children.empty() && (division::kind::splitter != children.back()->kind_of_division))
				{
					auto splitter = new div_splitter(tknizer.number());
					children.back()->div_next = splitter;
					children.emplace_back(splitter);
				}
				break;
			case token::div_start:
			{
				auto div = scan_div(tknizer);
				if (!children.empty())
					children.back()->div_next = div.get();

				children.emplace_back(div.release());
			}
				break;
			case token::vert:
				div_type = tk;
				break;
			case token::grid:
				div_type = tk;
				switch (tknizer.read())
				{
				case token::number:
					array.push_back(tknizer.number());
					array.push_back(tknizer.number());
					break;
				case token::array:
					tknizer.array().swap(array);
					break;
				case token::reparray:
					array.push_back(tknizer.reparray().at(0));
					array.push_back(tknizer.reparray().at(1));
					break;
                default:
                    break;
				}
				break;
			case token::collapse:
				if (tknizer.parameters().size() == 4)
				{
					auto get_number = [](const number_t & arg, const std::string& nth)
					{
						if (arg.kind_of() == number_t::kind::integer)
							return arg.integer();
						else if (arg.kind_of() == number_t::kind::real)
							return static_cast<int>(arg.real());

						throw std::invalid_argument("nana.place: the type of the "+ nth +" parameter for collapse should be integer.");
					};

					::nana::rectangle col;
					auto arg = tknizer.parameters().at(0);
					col.x = get_number(arg, "1st");

					arg = tknizer.parameters().at(1);
					col.y = get_number(arg, "2nd");

					arg = tknizer.parameters().at(2);
					col.width = static_cast<decltype(col.width)>(get_number(arg, "3rd"));

					arg = tknizer.parameters().at(3);
					col.height = static_cast<decltype(col.height)>(get_number(arg, "4th"));

					//Check the collapse area.
					//Ignore this collapse if its area is less than 2(col.width * col.height < 2)
					if (!col.empty() && (col.width > 1 || col.height > 1) && (col.x >= 0 && col.y >= 0))
					{
						//Overwrite if a exist_col in collapses has same position as the col.
						bool use_col = true;
						for (auto & exist_col : collapses)
						{
							if (exist_col.x == col.x && exist_col.y == col.y)
							{
								exist_col = col;
								use_col = false;
								break;
							}
						}
						if (use_col)
							collapses.emplace_back(col);
					}
				}
				else
					throw std::invalid_argument("nana.place: collapse requires 4 parameters.");
				break;
			case token::weight: case token::min_px: case token::max_px:
				{
					auto n = tknizer.number();
					//If n is the type of real, convert it to integer.
					//the integer and percent are allowed for weight/min/max.
					if (n.kind_of() == number_t::kind::real)
						n.assign(static_cast<int>(n.real()));

					switch (tk)
					{
					case token::weight: weight = n; break;
					case token::min_px: min_px = n; break;
					case token::max_px: max_px = n; break;
					default: break;	//Useless
					}
				}
				break;
			case token::arrange:
				arrange = tknizer.reparray();
				break;
			case token::gap:
				gap = tknizer.reparray();
				break;
			case token::margin:
				margin.clear();
				switch (tknizer.read())
				{
				case token::number:
					margin.set_value(tknizer.number());
					break;
				case token::array:
					margin.set_array(tknizer.array());
					break;
				case token::reparray:
					for (std::size_t i = 0; i < 4; ++i)
					{
						auto n = tknizer.reparray().at(i);
						if (n.is_none()) n.assign(0);
						margin.push(n);
					}
					break;
				default:
					break;
				}
				break;
			case token::div_end:
				exit_for = true;
				break;
			case token::identifier:
				name = tknizer.idstr();
				break;
			default:	break;
			}
			if (exit_for)
				break;
		}

		field_impl * attached_field = nullptr;
		if (name.size())
		{
			//find the field with specified name.
			//the field may not be created.
			auto i = fields.find(name);
			if (fields.end() != i)
			{
				attached_field = i->second;
				//the field is attached to a division, it means there is another division with same name.
				if (attached_field->attached)
				{
					bool do_throw = true;
					if (tmp_replaced)
					{
						auto att = attached_field->attached;
						if (att != tmp_replaced.get())
						{
							while (att->div_owner)
							{
								if (att->div_owner == tmp_replaced.get())
								{
									do_throw = false;
									break;
								}
								att = att->div_owner;
							}
						}
						else
							do_throw = false;
					}

					if (do_throw)
						throw std::invalid_argument("nana.place: the name '" + name + "' is redefined.");
					else
						attached_field = nullptr;
				}
			}

			if (search_div_name(this->root_division.get(), name))
				throw std::invalid_argument("nana.place: the name '" + name + "' is redefined.");
		}

		switch (div_type)
		{
		case token::eof:
		case token::vert:
			div.reset(new div_arrange(token::vert == div_type, std::move(name), std::move(arrange)));
			break;
		case token::grid:
		{
			std::unique_ptr<div_grid> p(new div_grid(std::move(name), std::move(arrange), std::move(collapses)));

			if (array.size())
				p->dimension.first = array[0].integer();

			if (array.size() > 1)
				p->dimension.second = array[1].integer();

			if (0 == p->dimension.first)
				p->dimension.first = 1;

			if (0 == p->dimension.second)
				p->dimension.second = 1;

			p->revise_collapses();
			div = std::move(p);
		}
			break;
		case token::dock:
			div.reset(new div_dock(std::move(name), this));
			break;
		default:
			throw std::invalid_argument("nana.place: invalid division type.");
		}

		//Requirements for min/max
		//1, min and max != negative
		//2, max >= min
		if (min_px.is_negative()) min_px.reset();
		if (max_px.is_negative()) max_px.reset();
		if (min_px.is_not_none() && max_px.is_not_none() && (min_px.get_value(100) > max_px.get_value(100)))
		{
			min_px.reset();
			max_px.reset();
		}

		//The weight will be ignored if one of min and max is specified.
		if (min_px.is_none() && max_px.is_none())
		{
			div->weight = weight;
		}
		else
		{
			div->min_px = min_px;
			div->max_px = max_px;
		}

		div->gap = std::move(gap);

		//attach the field to the division
		div->field = attached_field;
		if (attached_field)
			attached_field->attached = div.get();

		if (children.size())
		{
			if (division::kind::splitter == children.back()->kind_of_division)
			{
				children.pop_back();
				if (children.size())
					children.back()->div_next = nullptr;
			}

			for (auto& child : children)
			{
				child->div_owner = div.get();
				if (division::kind::splitter == child->kind_of_division)
					dynamic_cast<div_splitter&>(*child).direction(div_type != token::vert);
			}
		}

		div->children.swap(children);
		div->margin = std::move(margin);
		return div;
	}

	void place::implement::check_unique(const division* div) const
	{
		//The second field_impl is useless. Reuse the map type in order to
		//reduce the size of the generated code, becuase std::set<std::string>
		//will create a new template class.
		std::map<std::string, field_impl*> unique;
		field_impl tmp(nullptr);

		std::function<void(const implement::division* div)> check_fn;
		check_fn = [&check_fn, &unique, &tmp](const implement::division* div)
		{
			if (!div->name.empty())
			{
				auto & ptr = unique[div->name];
				if (ptr)
					throw std::invalid_argument("place, the name '" + div->name + "' is redefined.");
				ptr = &tmp;
			}

			for (auto & child : div->children)
			{
				check_fn(child.get());
			}
		};

		if (div)
			check_fn(div);
	}

	//class place
	place::place()
		: impl_(new implement)
	{}

	place::place(window wd)
		: impl_(new implement)
	{
		bind(wd);
	}

	place::~place()
	{
		delete impl_;
	}

	void place::bind(window wd)
	{
		if (impl_->window_handle)
			throw std::runtime_error("place.bind: it has already binded to a window.");

		impl_->window_handle = wd;
		impl_->event_size_handle = API::events(wd).resized.connect([this](const arg_resized& arg)
		{
			if (impl_->root_division)
			{
				impl_->root_division->field_area = ::nana::size(arg.width, arg.height);
				impl_->root_division->collocate(arg.window_handle);
			}
		});
	}

	window place::window_handle() const
	{
		return impl_->window_handle;
	}

	void place::div(const char* s)
	{
		place_parts::tokenizer tknizer(s);
		auto div = impl_->scan_div(tknizer);
		try
		{
			impl_->check_unique(div.get());	//may throw if there is a redefined name of field.
			impl_->root_division.reset();	//clear atachments div-fields
			impl_->root_division.swap(div);
		}
		catch (...)
		{
			//redefined a name of field
			throw;
		}
	}

	void place::modify(const char* name, const char* div_text)
	{
		if (nullptr == div_text)
			throw std::invalid_argument("nana.place: invalid div-text");

		if (nullptr == name) name = "";

		//check the name, it throws std::invalid_argument
		//if name violate the naming convention.
		place_parts::check_field_name(name);

		implement::division * div_ptr = nullptr;
		implement::field_impl * field_ptr = nullptr;
		{
			auto i = impl_->fields.find(name);
			if (i != impl_->fields.end())
				field_ptr = i->second;
		}

		if (field_ptr)
		{
			//remove the existing div object
			div_ptr = field_ptr->attached;
		}
		else
			div_ptr = impl_->search_div_name(impl_->root_division.get(), name);

		if (nullptr == div_ptr)
		{
			std::string what = "nana::place: field '";
			what += name;
			what += "' is not found.";

			throw std::invalid_argument(what);
		}


		std::unique_ptr<implement::division>* replaced = nullptr;

		implement::division * div_owner = div_ptr->div_owner;
		implement::division * div_next = div_ptr->div_next;
		implement::division * div_bro = nullptr;
		if (div_owner)
		{
			for (auto i = div_owner->children.begin(); i != div_owner->children.end(); ++i)
			{
				if (i->get() == div_ptr)
				{
					replaced = &(*i);
					break;
				}
				div_bro = i->get();
			}
		}
		else
			replaced = &(impl_->root_division);

		replaced->swap(impl_->tmp_replaced);

		try
		{
			place_parts::tokenizer tknizer(div_text);
			auto modified = impl_->scan_div(tknizer);
			auto modified_ptr = modified.get();
			modified_ptr->name = name;
			modified_ptr->field = field_ptr;

			replaced->swap(modified);

			impl_->check_unique(impl_->root_division.get());
			impl_->tmp_replaced.reset();

			modified_ptr->div_owner = div_owner;
			modified_ptr->div_next = div_next;
			if (field_ptr)
				field_ptr->attached = modified_ptr;

			std::function<void(implement::division*)> attach;
			attach = [this, &attach](implement::division* div)
			{
				if (!div->name.empty())
				{
					auto i = impl_->fields.find(div->name);
					if (impl_->fields.end() != i)
					{
						if (i->second->attached != div)
						{
							i->second->attached = div;
							div->field = i->second;
						}
					}
				}

				for (auto& child : div->children)
				{
					attach(child.get());
				}
			};

			attach(impl_->root_division.get());
		}
		catch (...)
		{
			replaced->swap(impl_->tmp_replaced);
			throw;
		}
	}

	place::field_reference place::field(const char* name)
	{
		if (nullptr == name)
			name = "";

		//check the name, it throws std::invalid_argument
		//if name violate the naming convention.
		place_parts::check_field_name(name);

		//get the field with specified name, if no such field with specified name
		//then create one.
		auto & p = impl_->fields[name];
		if (nullptr == p)
			p = new implement::field_impl(this);

		if ((!p->attached) && impl_->root_division)
		{
			//search the division with the specified name,
			//and attached the division to the field
			implement::division * div = implement::search_div_name(impl_->root_division.get(), name);
			if (div)
			{
				if (div->field && (div->field != p))
					throw std::runtime_error("nana.place: unexpected error, the division attachs a unexpected field.");

				div->field = p;
				p->attached = div;
			}
		}
		return *p;
	}

	void place::field_visible(const char* name, bool vsb)
	{
		if (!name)	name = "";
		
		//May throw std::invalid_argument
		place_parts::check_field_name(name);

		auto div = impl_->search_div_name(impl_->root_division.get(), name);
		if (div)
			div->set_visible(vsb);
	}

	bool place::field_visible(const char* name) const
	{
		if (!name)	name = "";

		//May throw std::invalid_argument
		place_parts::check_field_name(name);

		auto div = impl_->search_div_name(impl_->root_division.get(), name);
		return (div && div->visible);
	}

	void place::field_display(const char* name, bool dsp)
	{
		if (!name)	name = "";

		//May throw std::invalid_argument
		place_parts::check_field_name(name);

		auto div = impl_->search_div_name(impl_->root_division.get(), name);
		if (div)
			div->set_display(dsp);
	}

	bool place::field_display(const char* name) const
	{
		if (!name)	name = "";

		//May throw std::invalid_argument
		place_parts::check_field_name(name);

		auto div = impl_->search_div_name(impl_->root_division.get(), name);
		return (div && div->display);
	}

	void place::collocate()
	{
		impl_->collocate();
	}

	void place::erase(window handle)
	{
		bool recollocate = false;
		for (auto & fld : impl_->fields)
		{
			auto & elements = fld.second->elements;
			for (auto i = elements.begin(); i != elements.end();)
			{
				if (i->handle == handle)
				{
					API::umake_event(i->evt_destroy);
					i = elements.erase(i);
					recollocate |= (nullptr != fld.second->attached);
				}
				else
					++i;
			}

			auto & fastened = fld.second->fastened;
			for (auto i = fastened.begin(); i != fastened.end(); ++i)
			{
				if (i->handle == handle)
				{
					API::umake_event(i->evt_destroy);
					fastened.erase(i);
					break;
				}
			}
		}

		if (recollocate)
			collocate();
	}

	place::field_reference place::operator[](const char* name)
	{
		return field(name);
	}

	void place::_m_dock(const std::string& dockname, std::function<std::unique_ptr<widget>(window)> factory)
	{
		auto dock_ptr = dynamic_cast<implement::div_dock*>(impl_->search_div_name(impl_->root_division.get(), dockname));
		if (!dock_ptr)
			throw std::invalid_argument("nana.place: '" + dockname + "' is not a dock");

		dock_ptr->dockarea().add_factory(std::move(factory));
	}
	//end class place
}//end namespace nana
