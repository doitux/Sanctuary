# Credits

Sanctuary computes every sound in real time. It contains **no recordings and no sample data**.

| part | source | license |
|---|---|---|
| hall (16-line FDN, diffuser), FFT, CLAP test host | [OpenSection](https://github.com/doitux/OpenSection), same author | GPL-3.0-or-later |
| plugin framework (VST3, LV2, AU, standalone) | [JUCE](https://juce.com) 8 | AGPLv3 (combinable with GPLv3 code, GPLv3 § 13) |
| CLAP wrapper | [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions), [CLAP](https://github.com/free-audio/clap) | MIT |

The state-variable filter follows the topology-preserving design described by Vadim Zavalishin
(*The Art of VA Filter Design*) and Andrew Simper (Cytomic technical papers).
