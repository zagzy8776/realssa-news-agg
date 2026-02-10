\# RssGrabber Enhanced v5 - Global RSS Feed Aggregator



A high-performance C++ RSS feed aggregator fetching 2,300+ articles from 76 verified RSS feeds across 25+ countries.



\## Quick Start



\### Build

```bash

cl /EHsc /std:c++20 realssa\_news/realssa\_news.cpp /I"%VCPKG\_ROOT%\\installed\\x64-windows\\include" /link /LIBPATH:"%VCPKG\_ROOT%\\installed\\x64-windows\\lib" libcurl.lib pugixml.lib ws2\_32.lib crypt32.lib wldap32.lib /OUT:RssGrabber.exe

```



\### Run

```bash

.\\RssGrabber.exe

```



\## Features

\- 76 working RSS feeds (95%+ success rate)

\- Intelligent article summaries

\- Duplicate detection

\- Multi-format support (RSS 2.0, Atom, RDF)

\- JSON export



\## Coverage

\- United States (20 feeds): CNN, NYT, Vogue, Elle, IGN, TechCrunch, Wired

\- Europe (14 feeds): Le Monde, El Pais, Corriere, NZZ

\- Asia (13 feeds): China Daily, Yonhap, Soompi

\- Africa (8 feeds): News24, BellaNaija, TechCabal

\- Americas (6 feeds): Globo, Folha, IGN Brazil



\## License

MIT

