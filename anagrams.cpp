#include <algorithm>
#include <array>
#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <functional>
#include <numeric>
using namespace std;

constexpr int ALPHA = 32;
constexpr char NOCHAR = (char)-1;
constexpr int FORMAT_VERSION = 0x10010001;
typedef array<short, ALPHA> FreqCount;

#define ALIGN4(x) (((x) + 3) & -4)

struct Word {
	uint32_t index;
};
bool operator<(Word a, Word b) {
	return a.index < b.index;
}

struct Arena;

template<class T>
struct SmallPtr {
	uint32_t offset;
	SmallPtr(uint32_t offset) : offset(offset) {}
	T* get(Arena& base);
	const T* get(Arena& base) const;
};

template<class T>
bool operator==(SmallPtr<T> a, SmallPtr<T> b) {
	return a.offset == b.offset;
}

template<class T>
struct StackPtr {
	const Arena* arena;
	SmallPtr<T> inner;
	StackPtr(const Arena& arena, SmallPtr<T> inner) : arena(&arena), inner(inner) {}
	T* operator->() { return inner.get((Arena&)*arena); }
	T& operator[](size_t i) { return this->operator->()[i]; }
	T& operator*() { return (*this)[0]; }

	const T* operator->() const { return inner.get(*arena); }
	const T& operator[](size_t i) const { return this->operator->()[i]; }
	const T& operator*() const { return (*this)[0]; }
};

struct Arena {
	vector<char> mem;
	Arena() {}
	Arena(vector<char>&& mem) : mem(move(mem)) {}
	uint32_t size() const { return (uint32_t) mem.size(); }
	char* base() { return mem.data(); }
	const char* base() const { return mem.data(); }
	uint32_t rawAlloc(size_t size) {
		size = ALIGN4(size);
		uint32_t ret = this->size();
		size_t newSize = size + ret;
		assert(newSize <= (1U << 30) - 1 + (1U << 30));
		if (newSize > mem.capacity()) {
			mem.resize(mem.capacity() + newSize);
		}
		mem.resize(newSize);
		return (uint32_t) ret;
	}
	template<class T>
	StackPtr<T> alloc() {
		return StackPtr<T>(*this, rawAlloc(sizeof(T)));
	}
	template<class T>
	StackPtr<T> allocArray(size_t size) {
		return StackPtr<T>(*this, rawAlloc(sizeof(T) * size));
	}
};

template<class T>
T* SmallPtr<T>::get(Arena& base) {
	return (T*)(base.base() + offset);
}

template<class T>
const T* SmallPtr<T>::get(Arena& base) const {
	return (T*)(base.base() + offset);
}

struct Trie {
	// If isLeaf(), subs() represents a list of Word's of size count(),
	// otherwise it is a list of Trie's.
	// If isLeaf() && !isEmpty(), 'leaf()' is a NOCHAR-terminated string with
	// the characters that make up the words in the leaf, sorted.
	constexpr static int LEAF = 1 << 30;
	int subCount;
	bool isEmpty() const { return subCount == LEAF; }
	bool isLeaf() const { return subCount & LEAF; }
	int count() const { return subCount & ~LEAF; }
	const uint32_t* subs() const { return (const uint32_t*)this + 1; }
	const char* leaf() const {
		return (const char*) this + 4 + 4 * this->count();
	}
};

struct DS {
	Arena arena;

	// Lookup tables for letter normalization. The trie is based on letter
	// frequency in letter order 0, 1, ... and some letter orders are
	// (marginally) better than others, both wrt space and runtime.
	array<int, ALPHA> sortedLetters;
	array<int, ALPHA> letterOrder;

	SmallPtr<Trie> trie;

	// A chunk of null-terminated UTF-8 strings, pointed into by Word.index.
	SmallPtr<char> wordlist;

	using Callback = function<void(const vector<Word>&)>;
	void findAnagrams(const string& word, int numWords, Callback cb) const;

	void _findAnagrams(const FreqCount& wFreqCount, int at, vector<StackPtr<Trie>>& iters, FreqCount* leafFreqCounts, DS::Callback cb) const;

	void _rec(vector<StackPtr<Trie>>& iters, vector<Word>& sentence, Callback cb, int loopFrom) const;

	void _rec2(const FreqCount& wFreqCount, int at, vector<StackPtr<Trie>>& iters, FreqCount* leafFreqCounts, Callback cb, size_t at2, int freq, int maxFreq, int loopFrom) const;
};

string internalForm(const string& input, const array<int, ALPHA>& letterOrder) {
	string out;
	for (size_t i = 0; i < input.size(); i++) {
		char c = input[i];
		int e = -1;
		if ('a' <= c && c <= 'z')
			e = c - 'a';
		else if (c == (char)0xc3 && i + 1 != input.size()) {
			i++;
			char d = input[i];
			if (d == '\xa5') e = 26;
			else if (d == '\xa4') e = 27;
			else if (d == '\xb6') e = 28;
		}
		else if (c == '1')
			e = 29;
		else if (c == '3')
			e = 30;
		else if (c == '4')
			e = 31;
		if (e == -1) {
			cerr << "bad word: " << input << endl;
			exit(1);
		}
		out.push_back((char)letterOrder[e]);
	}
	sort(out.begin(), out.end());
	return out;
}

string externalForm(const string& input, const array<int, ALPHA>& sortedLetters) {
	string out;
	for (size_t i = 0; i < input.size(); i++) {
		int c = input[i];
		assert(0 <= c && c < ALPHA);
		c = sortedLetters[c];
		char d;
		if (c < 26) d = (char)('a' + c);
		else if (c < 29) {
			out += '\xc3';
			if (c == 26) d = '\xa5';
			else if (c == 27) d = '\xa4';
			else d = '\xb6';
		}
		else if (c == 29) d = '1';
		else if (c == 30) d = '3';
		else if (c == 31) d = '4';
		else {
			cerr << "Bad character " << c << endl;
			exit(1);
		}
		out += d;
	}
	return out;
}

SmallPtr<Trie> buildTrie(Arena& arena, vector<pair<string, vector<Word>>>& words, SmallPtr<Trie> nullTrie, int which) {
	if (words.size() == 0) {
		return nullTrie;
	} else if (words.size() == 1) {
		StackPtr<Trie> ret = arena.alloc<Trie>();
		string& s = words[0].first;
		auto& ws = words[0].second;
		StackPtr<uint32_t> leafWords = arena.allocArray<uint32_t>(ws.size());
		StackPtr<char> after = arena.allocArray<char>(s.size() + 1);
		memcpy(&after[0], s.c_str(), s.size());
		after[s.size()] = NOCHAR;
		int subCount = (int) ws.size();
		ret->subCount = subCount | Trie::LEAF;
		for (int i = 0; i < subCount; i++) {
			leafWords[i] = ws[i].index;
		}
		return ret.inner;
	} else {
		assert(which != ALPHA);
		StackPtr<Trie> ret = arena.alloc<Trie>();
		vector<vector<pair<string, vector<Word>>>> subwords;
		for (auto& pa : words) {
			int freq = 0;
			for (char c : pa.first) {
				if (c == which) freq++;
			}
			while (freq >= (int)subwords.size()) {
				subwords.emplace_back();
			}
			subwords[freq].push_back(move(pa));
		}
		words.clear();
		words.shrink_to_fit();

		int subCount = (int) subwords.size();
		ret->subCount = subCount;
		assert(subCount != 0);
		StackPtr<uint32_t> subs = arena.allocArray<uint32_t>(subwords.size());

		for (int i = 0; i < subCount; i++) {
			auto& sub = subwords[i];
			SmallPtr<Trie> subTrie = buildTrie(arena, sub, nullTrie, which + 1);
			subs[i] = subTrie.offset;
		}
		return ret.inner;
	}
}

void setFreqCount(StackPtr<Trie> t, FreqCount& freq) {
	assert(t->isLeaf());
	memset(freq.data(), 0, sizeof(freq));
	const char* c = t->leaf();
	while (*c != NOCHAR) {
		freq[*c]++;
		c++;
	}
}

map<string, int> readFreqs(const string& filename) {
	ifstream fin(filename);
	assert(fin);
	string line, word;
	map<string, int> freqs;
	while (getline(fin, line)) {
		size_t ind = line.find('\t');
		assert(ind != string::npos);
		word = line.substr(0, ind);
		for (char& c : word)
			c = (char)tolower(c);
		ind = line.rfind('\t');
		assert(ind > 0);
		size_t ind2 = line.rfind('\t', ind - 1);
		assert(ind2 > 0);
		freqs[word] += atoi(line.substr(ind2, ind - ind2).c_str());
	}
	return freqs;
}

DS buildDS(const string& dictFilename, const string& freqFilename) {
	map<string, int> freqs = readFreqs(freqFilename);
	Arena arena;
	string wordlist;
	ifstream fin(dictFilename);
	assert(fin);
	string line;
	map<string, vector<Word>> lookup;
	array<int, ALPHA> letterFreq{};
	array<int, ALPHA> letterOrder{};
	iota(letterOrder.begin(), letterOrder.end(), 0);
	while (getline(fin, line)) {
		size_t ind = line.find(' ');
		string word = internalForm(line.substr(0, ind), letterOrder);
		for (int c : word)
			letterFreq[c]++;
		if (ind != string::npos) {
			line = line.substr(ind + 1);
		}
		auto it = freqs.find(line);
		int freq = (it != freqs.end() ? it->second : 0);
		if (freq > 0xffff) freq = 0xffff;
		Word w{(uint32_t) wordlist.size()};
		wordlist += (char)(freq >> 8);
		wordlist += (char)(freq & 255);
		wordlist += line;
		wordlist += '\0';
		lookup[word].push_back(w);
	}

	// Come up with a reasonable letter traversal order. Empirically this
	// seems to do well, though it's somewhat unclear why.
	array<int, ALPHA> sortedLetters{};
	iota(sortedLetters.begin(), sortedLetters.end(), 0);
	sort(sortedLetters.begin(), sortedLetters.end(), [&](int a, int b) {
		return letterFreq[a] < letterFreq[b];
	});
	static_assert(ALPHA >= 20, "");
	reverse(sortedLetters.begin() + 20, sortedLetters.end());
	for (int i = 0; i < ALPHA; i++) {
		letterOrder[sortedLetters[i]] = i;
	}

	vector<pair<string, vector<Word>>> words(lookup.begin(), lookup.end());
	for (auto& pa : words) {
		for (char& c : pa.first)
			c = (char) letterOrder[(int) c];
	}

	StackPtr<Trie> nullTrie = arena.alloc<Trie>();
	nullTrie->subCount = Trie::LEAF;
	SmallPtr<Trie> trie = buildTrie(arena, words, nullTrie.inner, 0);
	assert(!trie.get(arena)->isEmpty());
	StackPtr<char> arenaWl = arena.allocArray<char>(wordlist.size());
	memcpy(&*arenaWl, wordlist.data(), wordlist.size());
	return {move(arena), sortedLetters, letterOrder, trie, arenaWl.inner};
}

void DS::findAnagrams(const string& word, int numWords, DS::Callback cb) const {
	string w = internalForm(word, this->letterOrder);
	FreqCount wFreqCount{};
	for (int c : w) {
		assert(0 <= c && c < ALPHA);
		wFreqCount[c]++;
	}
	vector<FreqCount> leafFreqCounts(numWords);
	StackPtr<Trie> tr{this->arena, this->trie};
	assert(!tr->isLeaf());
	vector<StackPtr<Trie>> iters(numWords, tr);
	_findAnagrams(wFreqCount, 0, iters, leafFreqCounts.data(), cb);
}

void DS::_rec(vector<StackPtr<Trie>>& iters, vector<Word>& sentence, DS::Callback cb, int loopFrom) const {
	if (sentence.size() == iters.size()) {
		cb(sentence);
	} else {
		size_t at = sentence.size();
		StackPtr<Trie> t = iters[at];
		assert(t->isLeaf());
		int sc = t->count();
		assert(sc != 0);
		for (int i = loopFrom; i < sc; i++) {
			sentence.push_back(Word{t->subs()[i]});
			int nextFrom = 0;
			if (at + 1 != iters.size() && t.inner == iters[at + 1].inner) {
				// Enforce sorted output order
				nextFrom = i;
			}
			_rec(iters, sentence, cb, nextFrom);
			sentence.pop_back();
		}
	}
}

void DS::_rec2(const FreqCount& wFreqCount, int at, vector<StackPtr<Trie>>& iters, FreqCount* leafFreqCounts, DS::Callback cb, size_t at2, int freq, int maxFreq, int loopFrom) const {
	// freq: remaining frequency to distribute
	// maxFreq: how much more frequency we can distribute over the rest of the iterators
	// (both counts ignore leaf nodes)
	if (freq > maxFreq) return;
	if (at2 == iters.size()) {
		assert(maxFreq == 0);
		assert(freq == 0);
		this->_findAnagrams(wFreqCount, at, iters, leafFreqCounts, cb);
		return;
	}
	StackPtr<Trie> t = iters[at2];
	if (t->isLeaf()) {
		this->_rec2(wFreqCount, at, iters, leafFreqCounts, cb, at2 + 1, freq, maxFreq, 0);
	} else {
		int mf = t->count() - 1;
		for (int f = loopFrom; f <= min(freq, mf); f++) {
			StackPtr<Trie> nt{this->arena, SmallPtr<Trie>(t->subs()[f])};
			if (nt->isEmpty()) continue;
			if (nt->isLeaf()) {
				setFreqCount(nt, leafFreqCounts[at2]);
			}
			iters[at2] = nt;
			int nextFrom = 0;
			if (at2 + 1 != iters.size() && t.inner == iters[at2 + 1].inner) {
				// Enforce sorted output order
				nextFrom = f;
			}
			this->_rec2(wFreqCount, at, iters, leafFreqCounts, cb, at2 + 1, freq - f, maxFreq - mf, nextFrom);
		}
		iters[at2] = t;
	}
}

void DS::_findAnagrams(const FreqCount& wFreqCount, int at, vector<StackPtr<Trie>>& iters, FreqCount* leafFreqCounts, DS::Callback cb) const {
	if (at == ALPHA) {
		vector<Word> sentence;
		this->_rec(iters, sentence, cb, 0);
		return;
	}
	assert(at != ALPHA);
	int freq = wFreqCount[at], maxFreq = 0;
	for (size_t i = 0; i < iters.size(); i++) {
		if (iters[i]->isLeaf()) {
			freq -= leafFreqCounts[i][at];
		} else {
			maxFreq += iters[i]->count() - 1;
		}
	}
	if (freq < 0 || freq > maxFreq) {
		return;
	}
	this->_rec2(wFreqCount, at + 1, iters, leafFreqCounts, cb, 0, freq, maxFreq, 0);
}

void writeDS(DS& ds, const string& filename) {
	ofstream file(filename, ios::binary);
	file.exceptions(ios::failbit | ios::badbit | ios::eofbit);
	try {
		uint32_t header[4] = {
			FORMAT_VERSION,
			ds.arena.size(),
			ds.trie.offset,
			ds.wordlist.offset,
		};
		file.write((const char*) &header[0], sizeof(header));
		file.write((const char*) &ds.sortedLetters[0], sizeof(ds.sortedLetters));
		file.write((const char*) &ds.letterOrder[0], sizeof(ds.letterOrder));
		file.write(ds.arena.mem.data(), ds.arena.size());
	}
	catch (const ios_base::failure& exc) {
		cerr << "Failed to write " << filename << ": " << exc.what() << endl;
		exit(1);
	}
}

DS readDS(const string& filename) {
	// TODO: mmap
	ifstream file(filename, ios::binary);
	file.exceptions(ios::failbit | ios::badbit | ios::eofbit);
	try {
		uint32_t header[4];
		file.read((char*) &header[0], sizeof(header));
		uint32_t version = header[0];
		uint32_t arenaSize = header[1];
		uint32_t triePtr = header[2];
		uint32_t wordlistPtr = header[3];
		if (version != FORMAT_VERSION) {
			cerr << "Bad file format." << endl;
			exit(1);
		}
		array<int, ALPHA> sortedLetters;
		array<int, ALPHA> letterOrder;
		file.read((char*) &sortedLetters[0], sizeof(sortedLetters));
		file.read((char*) &letterOrder[0], sizeof(letterOrder));
		vector<char> mem(arenaSize);
		file.read(mem.data(), arenaSize);
		return DS{Arena(move(mem)), sortedLetters, letterOrder,
			SmallPtr<Trie>(triePtr), SmallPtr<char>(wordlistPtr)};
	}
	catch (const ios_base::failure& exc) {
		cerr << "Failed to read " << filename << ": " << exc.what() << endl;
		exit(1);
	}
}

void help(const char* program) {
	cout << "Usage:" << endl;
	cout << program << " --help | --build <dict.txt> <stats.txt> |"
		" [--limit <N>]"
		" [--max-words <N>]"
		" [--file <file.bin>]"
		" [<word>...]"
		<< endl;
	exit(0);
}

int main(int argc, char** argv) {
	int limit = 1000, maxWords = 1;
	string dsFilename = "dict.bin";
	bool noMoreFlags = false;
	bool build = false;
	vector<string> nonflags;

	for (int i = 1; i < argc; i++) {
		string a = argv[i];
		auto next = [&]() -> string {
			if (i + 1 == argc) {
				cerr << "Missing argument parameter" << endl;
				exit(1);
			}
			i++;
			return argv[i];
		};
		if (!noMoreFlags && !a.empty() && a[0] == '-') {
			if (a == "--") {
				noMoreFlags = true;
			} else if (a == "--help") {
				help(argv[0]);
			} else if (a == "--build") {
				build = true;
			} else if (a == "--limit") {
				string x = next();
				limit = atoi(x.c_str());
			} else if (a == "--max-words") {
				string x = next();
				maxWords = atoi(x.c_str());
			} else if (a == "--file") {
				dsFilename = next();
			} else {
				cerr << "Unrecognized flag " << a << endl;
				return 1;
			}
		} else {
			nonflags.push_back(a);
		}
	}

	if (build) {
		if (nonflags.size() != 2) {
			cerr << "Invalid arguments." << endl;
			return 1;
		}
		DS ds = buildDS(nonflags[0], nonflags[1]);
		cout << "ds size: " << ds.arena.size() << endl;
		writeDS(ds, dsFilename);
		exit(0);
	}

	DS ds = readDS(dsFilename);

	auto run = [&](const string& word) {
		int count = 0;
		try {
			for (int numWords = 1; count < limit && numWords <= maxWords; numWords++) {
				vector<pair<double, string>> sentences;
				ds.findAnagrams(word, numWords, [&](const vector<Word>& sentence_) -> void {
					vector<Word> sentence = sentence_;
					sort(sentence.begin(), sentence.end());
					StackPtr<char> wordlist{ds.arena, ds.wordlist};
					bool first = true;
					double totFreq = 1;
					string output;
					for (Word w : sentence) {
						if (first) first = false;
						else output += ' ';
						const char* cw = &wordlist[w.index];
						int freq = (unsigned char)cw[0] * 256 + (unsigned char)cw[1];
						totFreq *= freq + 1;
						output += cw + 2;
					}
					sentences.emplace_back(totFreq, output);
				});
				sort(sentences.begin(), sentences.end(), greater<void>());
				for (auto& pa : sentences) {
					cout << pa.second << endl;
					count++;
					if (count == limit) {
						cout << "<reached limit of " << limit << ", pass --limit to raise>" << endl;
						throw false;
					}
				}
			}
		}
		catch (bool) {}
	};

	if (nonflags.size() != 0) {
		for (string& word : nonflags) {
			run(word);
		}
	} else {
		for (;;) {
			cout << "> ";
			string word;
			cin >> word;
			if (!cin || word.empty()) break;
			run(word);
			cout << endl;
		}
	}
}
