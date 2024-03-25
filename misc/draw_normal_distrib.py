import matplotlib.pyplot as plt
import numpy as np
import scipy.stats as stats
import math

http_mu  = 4.2
http_std = 28.99

nginx_mu  = 160.88
nginx_std = 234.39


def draw_normal(mu, std):

    mu = 0
    sigma = std
    x = np.linspace(mu - 3*sigma, mu + 3*sigma, 100)
    plt.plot(x, stats.norm.pdf(x, mu, sigma))

draw_normal(http_mu, http_std)
draw_normal(nginx_mu, nginx_std)

plt.savefig('foo.png')
#plt.show()
